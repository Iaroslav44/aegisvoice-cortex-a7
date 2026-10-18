/* Executable deterministic RTOS/HAL contract test. The actual production task
 * loops run against bounded queues and latched notifications. This tests task
 * orchestration, not FreeRTOS internals, IRQ preemption or target WCET. */
#include <setjmp.h>
#include <stdlib.h>
#include "../firmware/av_firmware.c"

#define REQUIRE(x) do { if (!(x)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); \
} } while (0)

typedef struct {
    size_t capacity, item_size, count, head;
    uint8_t *storage;
} fake_queue;
typedef struct {
    TaskFunction_t entry;
    uint32_t notifications;
} fake_task;
typedef struct { unsigned at; void (*action)(void); } scheduled_event;
static fake_queue queues[3];
static fake_task tasks[4], *running;
static unsigned queue_count, task_count, now_ms, delays, waits, played_count;
static TickType_t tick_origin;
static const scheduled_event *events;
static unsigned event_index;
static unsigned resets, begins, decrypts, encrypts, releases, playback_resets;
static unsigned rx_remaining, rx_calls, tx_calls, usb_reads, feeds;
static unsigned usb_remaining, decode_stall;
static bool physical_connected;
static uint32_t physical_epoch;
static bool race_on_empty, expect_fatal;
static const char *fatal_reason;
static int16_t capture_pcm[AV_PCM_SAMPLES];
static struct { unsigned at; int16_t value; } played[512];
static jmp_buf finished;

void av_check_assert(const char *file, unsigned line)
{
    fprintf(stderr, "RTOS assert %s:%u\n", file, line); exit(1);
}
void av_check_enter_critical(void) { }
void av_check_exit_critical(void) { }
void av_check_yield(BaseType_t wake) { (void)wake; }

QueueHandle_t xQueueCreateStatic(UBaseType_t capacity, UBaseType_t item_size,
                                 uint8_t *storage, StaticQueue_t *control)
{
    (void)control;
    REQUIRE(queue_count < 3u);
    fake_queue *q = &queues[queue_count++];
    *q = (fake_queue){capacity, item_size, 0u, 0u, storage};
    return q;
}
BaseType_t xQueueSend(QueueHandle_t handle, const void *item, TickType_t timeout)
{
    fake_queue *q = handle;
    REQUIRE(timeout == 0u);
    if (q->count == q->capacity) return pdFALSE;
    size_t index = (q->head + q->count) % q->capacity;
    memcpy(q->storage + index * q->item_size, item, q->item_size);
    ++q->count;
    return pdPASS;
}
BaseType_t xQueueReceive(QueueHandle_t handle, void *item, TickType_t timeout)
{
    fake_queue *q = handle;
    REQUIRE(timeout == 0u);
    if (q->count == 0u) return pdFALSE;
    memcpy(item, q->storage + q->head * q->item_size, q->item_size);
    q->head = (q->head + 1u) % q->capacity;
    --q->count;
    return pdPASS;
}
BaseType_t xQueueSendFromISR(QueueHandle_t handle, const void *item, BaseType_t *wake)
{
    (void)wake;
    return xQueueSend(handle, item, 0);
}
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t handle)
{
    UBaseType_t count = (UBaseType_t)((fake_queue *)handle)->count;
    if (race_on_empty && handle == capture_queue && count == 0u) {
        race_on_empty = false;
        REQUIRE(av_firmware_capture_from_isr(0));
    }
    return count;
}
TaskHandle_t xTaskCreateStatic(TaskFunction_t entry, const char *name,
                              configSTACK_DEPTH_TYPE depth, void *context,
                              UBaseType_t priority, StackType_t *stack,
                              StaticTask_t *control)
{
    (void)name; (void)depth; (void)context; (void)priority; (void)stack; (void)control;
    REQUIRE(task_count < 4u);
    fake_task *task = &tasks[task_count++];
    *task = (fake_task){entry, 0u};
    return task;
}
BaseType_t xTaskNotifyGive(TaskHandle_t handle)
{
    fake_task *task = handle;
    REQUIRE(task != NULL);
    ++task->notifications;
    return pdPASS;
}
void vTaskNotifyGiveFromISR(TaskHandle_t handle, BaseType_t *wake)
{
    *wake = pdTRUE;
    (void)xTaskNotifyGive(handle);
}
static void advance(unsigned until, bool interruptible)
{
    while (events[event_index].at <= until) {
        REQUIRE(events[event_index].at >= now_ms);
        now_ms = events[event_index].at;
        void (*action)(void) = events[event_index++].action;
        action();
        if (interruptible && running->notifications != 0u) return;
    }
    now_ms = until;
}
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t timeout)
{
    REQUIRE(clear == pdTRUE);
    if (running->notifications == 0u && timeout != 0u) {
        ++waits;
        advance(now_ms + timeout, true);
    }
    uint32_t count = running->notifications;
    running->notifications = 0;
    return count;
}
void vTaskDelay(TickType_t ticks)
{
    REQUIRE(ticks != 0u);
    ++delays;
    advance(now_ms + ticks, false);
}
TickType_t xTaskGetTickCount(void) { return tick_origin + now_ms; }
TickType_t xTaskGetTickCountFromISR(void) { return xTaskGetTickCount(); }

static void reset_session(void *context) { (void)context; ++resets; }
static bool begin_session(void *context, uint8_t *packet, size_t *length)
{
    (void)context; (void)packet; ++begins; *length = 0; return true;
}
static bool control_packet(void *context, const uint8_t *packet, size_t length,
                            uint8_t *response, size_t *response_length,
                            bool *authenticated)
{
    (void)context; (void)response;
    REQUIRE(length == 1u);
    *response_length = 0;
    *authenticated = packet[0] == 1u;
    return true;
}
static bool encrypt_frame(void *context, const int16_t pcm[AV_PCM_SAMPLES],
                           uint8_t *packet, size_t *length)
{
    (void)context; (void)pcm;
    ++encrypts; REQUIRE(*length >= 1u); packet[0] = 42u; *length = 1u; return true;
}
static bool decrypt_frame(void *context, const uint8_t *packet, size_t length,
                           int16_t pcm[AV_PCM_SAMPLES])
{
    (void)context;
    ++decrypts; REQUIRE(length == 1u);
    now_ms += decode_stall; decode_stall = 0;
    int16_t value = packet[0] == 0xeeu ? 0 : packet[0];
    for (unsigned i = 0; i < AV_PCM_SAMPLES; ++i) pcm[i] = value;
    return packet[0] != 0xeeu;
}
static bool identity(void *context, uint8_t out[32])
{
    (void)context; memset(out, 42, 32u); return true;
}
static bool link_state(void *context, uint32_t *epoch, bool *connected)
{
    (void)context; *epoch = physical_epoch; *connected = physical_connected; return true;
}
static bool receive_packet(void *context, bool *control, uint8_t *packet, size_t *length)
{
    (void)context; ++rx_calls;
    if (rx_remaining == 0u) { *length = 0; return true; }
    --rx_remaining; *length = 1; packet[0] = 5; *control = false; return true;
}
static bool send_packet(void *context, uint32_t epoch, bool control,
                         const uint8_t *packet, size_t length)
{
    (void)context; (void)control; (void)packet;
    REQUIRE(length == 1u);
    if (epoch != physical_epoch || !physical_connected) return false;
    ++tx_calls; return true;
}
static bool claim(void *context, uint8_t id, const int16_t **pcm)
{
    (void)context; (void)id; *pcm = capture_pcm; return true;
}
static void release(void *context, uint8_t id)
{
    (void)context; (void)id; ++releases; memset(capture_pcm, 0, sizeof(capture_pcm));
}
static bool playback(void *context, uint32_t epoch, const int16_t pcm[AV_PCM_SAMPLES])
{
    (void)context;
    REQUIRE(played_count < 512u);
    if (epoch != physical_epoch) return false;
    for (unsigned i = 1; i < AV_PCM_SAMPLES; ++i) REQUIRE(pcm[i] == pcm[0]);
    played[played_count].at = now_ms;
    played[played_count++].value = pcm[0];
    return true;
}
static void playback_reset(void *context) { (void)context; ++playback_resets; }
static size_t usb_read(void *context, uint8_t *bytes, size_t capacity)
{
    (void)context; (void)capacity; ++usb_reads;
    if (usb_remaining == 0u) return 0;
    --usb_remaining; bytes[0] = '\n'; return 1;
}
static bool usb_write(void *context, const uint8_t *bytes, size_t length)
{
    (void)context; (void)bytes; (void)length; return true;
}
static bool feed(void *context) { (void)context; ++feeds; return true; }
static void fatal_error(void *context, const char *reason)
{
    fatal_reason = reason;
    if (expect_fatal) longjmp(finished, 1);
    (void)context; fprintf(stderr, "unexpected firmware fatal: %s\n", reason); exit(1);
}
static void setup(void)
{
    av_firmware_app_ops application = {NULL, reset_session, begin_session,
        control_packet, encrypt_frame, decrypt_frame, identity};
    av_firmware_hal_ops hardware = {NULL, link_state, receive_packet, send_packet,
        claim, release, playback, playback_reset, usb_read, usb_write, feed, fatal_error};
    queue_count = task_count = now_ms = delays = waits = played_count = 0;
    resets = begins = decrypts = encrypts = releases = playback_resets = 0;
    rx_remaining = rx_calls = tx_calls = usb_reads = feeds = event_index = 0;
    usb_remaining = decode_stall = 0;
    physical_epoch = tick_origin = 0;
    physical_connected = race_on_empty = expect_fatal = created = false;
    fatal_reason = NULL;
    REQUIRE(av_firmware_create(&application, &hardware));
}
static void stop(void) { longjmp(finished, 1); }
static void run(unsigned task_index, const scheduled_event *schedule)
{
    events = schedule; event_index = 0; running = &tasks[task_index];
    if (setjmp(finished) == 0) running->entry(NULL);
}
static void publish_link(bool connected)
{
    ++physical_epoch;
    physical_connected = connected;
    health.epoch = physical_epoch;
    health.connected = connected;
    health.authenticated = false;
    xTaskNotifyGive(media_handle);
}
static void connect_peer(void) { publish_link(true); }
static void disconnect_peer(void) { publish_link(false); }
static bool packet(uint8_t value, bool control, uint32_t epoch)
{
    wire_event wire = {0};
    wire.data[0] = value; wire.length = 1u; wire.control = control; wire.epoch = epoch;
    if (xQueueSend(incoming_queue, &wire, 0) != pdPASS) return false;
    xTaskNotifyGive(media_handle); return true;
}
static void authenticate(void) { REQUIRE(packet(1u, true, physical_epoch)); }
static void incomplete_control(void) { REQUIRE(packet(0u, true, physical_epoch)); }
static void capture(void) { REQUIRE(av_firmware_capture_from_isr(0)); }
static void authenticate_and_old_capture(void)
{
    capture(); authenticate(); /* Completion predates the accepted handshake. */
}
static void four_frames(void)
{
    for (uint8_t n = 11u; n < 15u; ++n) REQUIRE(packet(n, false, physical_epoch));
}
static void invalid_frame(void) { REQUIRE(packet(0xeeu, false, physical_epoch)); }
static void stale_capture(void)
{
    capture_event event = {0, xTaskGetTickCount() - pdMS_TO_TICKS(AV_FRAME_MS),
                           physical_epoch, true};
    REQUIRE(xQueueSend(capture_queue, &event, 0) == pdPASS);
    xTaskNotifyGive(media_handle);
}
static void old_epoch(void)
{
    REQUIRE(packet(1u, false, physical_epoch - 1u));
    capture_event event = {0, xTaskGetTickCount(), physical_epoch - 1u, true};
    REQUIRE(xQueueSend(capture_queue, &event, 0) == pdPASS);
    xTaskNotifyGive(media_handle);
}
static void full_queues(void)
{
    for (uint8_t n = 1u; n <= AV_FW_QUEUE; ++n) REQUIRE(packet(n, false, physical_epoch));
    REQUIRE(!packet(99u, false, physical_epoch));
    for (uint8_t n = 0u; n < AV_CAPTURE_QUEUE; ++n) REQUIRE(av_firmware_capture_from_isr(n));
    REQUIRE(!av_firmware_capture_from_isr(99u));
}
static void two_slow_frames(void)
{
    REQUIRE(packet(10u, false, physical_epoch));
    REQUIRE(packet(11u, false, physical_epoch));
    decode_stall = 35u;
}
static void two_very_slow_frames(void) { two_slow_frames(); decode_stall = 65u; }
static void arm_empty_race(void) { race_on_empty = true; }
static void healthy_workers(void)
{
    for (unsigned i = 0; i < 3u; ++i) health.heartbeat[i] = xTaskGetTickCount();
    health.capture_tick = xTaskGetTickCount();
    health.capture_seen = true;
}
static void workers_without_capture(void)
{
    for (unsigned i = 0; i < 3u; ++i) health.heartbeat[i] = xTaskGetTickCount();
}
static void bt_arrival(void)
{
    rx_remaining = 1;
    av_firmware_bluetooth_notify_from_isr();
}
static void usb_arrival(void) { av_firmware_usb_notify(); }
static void physical_disconnect(void)
{
    ++physical_epoch; physical_connected = false;
    av_firmware_bluetooth_notify();
}

int main(void)
{
    const scheduled_event one_second[] = {{1000u, stop}};
    setup(); run(0, one_second);
    REQUIRE(waits == 50u && delays == 0u && played_count == 49u);
    setup(); run(1, one_second);
    REQUIRE(waits == 20u && delays == 0u && rx_calls == 0u);
    setup(); run(2, one_second);
    REQUIRE(waits == 20u && delays == 0u && usb_reads == 20u);

    const scheduled_event timeout[] = {{1000u, incomplete_control},
        {2000u, incomplete_control}, {4000u, incomplete_control}, {5020u, stop}};
    setup(); connect_peer(); run(0, timeout);
    REQUIRE(begins == 1u && resets == 2u && health.rejects == 1u);
    REQUIRE(!health.authenticated && delays == 0u);
    const scheduled_event preauth[] = {{1u, authenticate_and_old_capture}, {2u, stop}};
    setup(); connect_peer(); run(0, preauth);
    REQUIRE(health.authenticated && encrypts == 0u && releases == 1u);

    const scheduled_event media[] = {{1u, authenticate}, {2u, capture},
        {3u, four_frames}, {5u, invalid_frame}, {81u, stale_capture},
        {82u, old_epoch}, {90u, disconnect_peer}, {102u, stop}};
    setup(); connect_peer(); run(0, media);
    REQUIRE(encrypts == 1u && releases == 3u && decrypts == 5u);
    REQUIRE(health.drops == 1u && health.rejects == 1u && health.underruns == 1u);
    REQUIRE(played_count == 4u);
    REQUIRE(played[0].at == 20u && played[0].value == 12);
    REQUIRE(played[1].at == 40u && played[1].value == 13);
    REQUIRE(played[2].at == 60u && played[2].value == 14);
    REQUIRE(played[3].at == 80u && played[3].value == 0);
    REQUIRE(resets == 2u && playback_resets >= 2u && !health.authenticated);

    const scheduled_event flood[] = {{1u, authenticate}, {2u, full_queues}, {81u, stop}};
    setup(); connect_peer(); run(0, flood);
    REQUIRE(releases == 4u && decrypts == 8u && health.drops == 5u);
    REQUIRE(delays == 3u && played_count == 4u);
    REQUIRE(played[0].value == 6 && played[1].value == 7 && played[2].value == 8);
    REQUIRE(played[3].value == 0 && health.underruns == 1u);

    const scheduled_event lateness[] = {{1u, authenticate}, {2u, two_slow_frames}, {61u, stop}};
    setup(); connect_peer(); run(0, lateness);
    REQUIRE(played_count == 3u && played[0].at == 37u && played[1].at == 40u);
    REQUIRE(played[0].value == 10 && played[1].value == 11 && played[2].value == 0);
    setup(); tick_origin = UINT32_MAX - 9u; connect_peer(); run(0, lateness);
    REQUIRE(played[0].at == 37u && played[1].at == 40u);
    const scheduled_event missed[] = {{1u, authenticate}, {2u, two_very_slow_frames}, {81u, stop}};
    setup(); connect_peer(); run(0, missed);
    REQUIRE(played_count == 2u && played[0].at == 67u && played[1].at == 80u);
    REQUIRE(played[0].value == 0 && health.drops == 2u);
    const scheduled_event race[] = {{1u, authenticate}, {2u, arm_empty_race}, {21u, stop}};
    setup(); connect_peer(); capture(); run(0, race);
    REQUIRE(!race_on_empty && encrypts == 1u && releases == 2u);

    const scheduled_event bt_event[] = {{7u, bt_arrival}, {8u, stop}};
    setup(); physical_connected = true; physical_epoch = 1; run(1, bt_event);
    REQUIRE(uxQueueMessagesWaiting(incoming_queue) == 1u && now_ms == 8u);
    REQUIRE(tasks[0].notifications >= 2u && delays == 0u);
    const scheduled_event bt_flood[] = {{11u, stop}};
    setup(); physical_connected = true; physical_epoch = 1; rx_remaining = 20;
    run(1, bt_flood);
    REQUIRE(uxQueueMessagesWaiting(incoming_queue) == 8u && health.drops == 12u);
    REQUIRE(delays == 10u && waits == 1u);
    const scheduled_event tx_disconnect[] = {{1u, physical_disconnect}, {2u, stop}};
    setup(); physical_connected = true; physical_epoch = 1u;
    health.epoch = 1u; health.authenticated = true;
    wire_event tx = {0}; tx.length = 1u;
    REQUIRE(xQueueSend(outgoing_queue, &tx, 0) == pdPASS); /* Already obsolete. */
    tx.epoch = 1u;
    REQUIRE(xQueueSend(outgoing_queue, &tx, 0) == pdPASS);
    REQUIRE(xQueueSend(outgoing_queue, &tx, 0) == pdPASS); /* Obsolete after yield. */
    run(1, tx_disconnect);
    REQUIRE(tx_calls == 1u && uxQueueMessagesWaiting(outgoing_queue) == 0u);
    REQUIRE(!health.authenticated && health.epoch == 2u && !health.connected);
    const scheduled_event usb_event[] = {{7u, usb_arrival}, {8u, stop}};
    setup(); run(2, usb_event); REQUIRE(usb_reads == 2u && delays == 0u);
    const scheduled_event usb_chunks[] = {{4u, stop}};
    setup(); usb_remaining = 3u; run(2, usb_chunks);
    REQUIRE(usb_reads == 4u && delays == 3u && waits == 1u);

    const scheduled_event healthy[] = {{50u, healthy_workers}, {100u, healthy_workers},
        {150u, healthy_workers}, {200u, healthy_workers}, {250u, healthy_workers}, {301u, stop}};
    setup(); run(3, healthy); REQUIRE(feeds == 7u && !health.fatal_error);
    const scheduled_event no_capture[] = {{50u, workers_without_capture},
        {100u, workers_without_capture}, {150u, workers_without_capture},
        {200u, workers_without_capture}, {250u, workers_without_capture}, {301u, stop}};
    setup(); expect_fatal = true; run(3, no_capture);
    REQUIRE(feeds == 5u && health.fatal_error && strcmp(fatal_reason, "watchdog health") == 0);
    setup(); expect_fatal = true; run(3, one_second);
    REQUIRE(now_ms == 250u && feeds == 5u && health.fatal_error);

    puts("firmware runtime: event waits, wake races, bounded floods, timeout, epochs, "
         "capture ownership, jitter rejection/overflow/underrun, phase and tick wrap passed");
    return 0;
}
