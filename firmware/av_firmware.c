#include "av_firmware.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <stdio.h>
#include <string.h>

#define AV_FW_QUEUE 8u
#define AV_CAPTURE_QUEUE 4u
#define AV_JITTER_FRAMES 3u
/* A spare decode slot preserves queued speech when authentication fails. */
#define AV_JITTER_SLOTS (AV_JITTER_FRAMES + 1u)
#define AV_MEDIA_STACK 4096u
#define AV_IO_STACK 1024u
#define AV_SUP_STACK 512u
#define AV_HANDSHAKE_TIMEOUT_MS 5000u
#define AV_IO_HEARTBEAT_MS 50u

typedef struct {
    uint8_t id;
    TickType_t tick;
    uint32_t epoch;
    bool authenticated;
} capture_event;
typedef struct {
    uint32_t epoch;
    bool control;
    size_t length;
    uint8_t data[AV_MAX_PACKET];
} wire_event;
typedef struct {
    TickType_t heartbeat[3];
    TickType_t capture_tick;
    uint32_t epoch;
    uint32_t drops;
    uint32_t rejects;
    uint32_t underruns;
    bool connected;
    bool authenticated;
    bool capture_seen;
    bool fatal_error;
} health_state;

static av_firmware_app_ops app;
static av_firmware_hal_ops hal;
static volatile health_state health;
static bool created;
static uint8_t public_id[32];
static QueueHandle_t capture_queue, incoming_queue, outgoing_queue;
static StaticQueue_t capture_control, incoming_control, outgoing_control;
static uint8_t capture_storage[AV_CAPTURE_QUEUE * sizeof(capture_event)];
static uint8_t incoming_storage[AV_FW_QUEUE * sizeof(wire_event)];
static uint8_t outgoing_storage[AV_FW_QUEUE * sizeof(wire_event)];
static StaticTask_t task_control[4], idle_control;
static TaskHandle_t media_handle, bluetooth_handle, usb_handle;
static StackType_t media_stack[AV_MEDIA_STACK];
static StackType_t bt_stack[AV_IO_STACK], usb_stack[AV_IO_STACK];
static StackType_t supervisor_stack[AV_SUP_STACK];
static StackType_t idle_stack[configMINIMAL_STACK_SIZE];
static const int16_t silence[AV_PCM_SAMPLES] = {0};

static void erase(void *p, size_t length)
{
    volatile uint8_t *bytes = p;
    while (length-- != 0u) *bytes++ = 0;
}

static void fail(const char *reason)
{
    taskENTER_CRITICAL();
    health.fatal_error = true;
    taskEXIT_CRITICAL();
    hal.fatal(hal.context, reason);
    /* Contract violation: never feed watchdog if BSP fatal unexpectedly returns. */
    for (;;) vTaskDelay(pdMS_TO_TICKS(100));
}

static void beat(unsigned task)
{
    taskENTER_CRITICAL();
    health.heartbeat[task] = xTaskGetTickCount();
    taskEXIT_CRITICAL();
}

static void increment(volatile uint32_t *counter)
{
    taskENTER_CRITICAL();
    if (*counter != UINT32_MAX) ++*counter;
    taskEXIT_CRITICAL();
}

static health_state snapshot(void)
{
    health_state copy;
    taskENTER_CRITICAL();
    copy = health;
    taskEXIT_CRITICAL();
    return copy;
}

static bool queue_packet(uint32_t epoch, bool control, wire_event *packet)
{
    packet->epoch = epoch;
    packet->control = control;
    if (packet->length == 0u) return true;
    if (packet->length > AV_MAX_PACKET) return false;
    if (xQueueSend(outgoing_queue, packet, 0) != pdPASS) {
        increment(&health.drops);
        return false;
    }
    xTaskNotifyGive(bluetooth_handle);
    return true;
}

static void media_task(void *unused)
{
    uint32_t epoch = 0;
    bool session_initialized = false;
    bool authenticated = false;
    bool session_failed = false;
    int16_t jitter[AV_JITTER_SLOTS][AV_PCM_SAMPLES];
    /* One slot stays free: equal indices mean empty. */
    unsigned read_index = 0, write_index = 0;
    TickType_t next_playout = xTaskGetTickCount();
    TickType_t handshake_started = next_playout;
    (void)unused;
    for (;;) {
        /* Drain old wake tokens before examining work. An event arriving after
         * this point stays latched, including the check-to-sleep race. */
        (void)ulTaskNotifyTake(pdTRUE, 0);
        health_state state = snapshot();
        /* Only the initialized public/ciphertext prefix is sent. */
        wire_event wire;
        capture_event capture;
        if (!session_initialized || state.epoch != epoch) {
            session_initialized = true;
            epoch = state.epoch;
            authenticated = false;
            session_failed = false;
            app.reset_session(app.context);
            hal.playback_reset(hal.context);
            erase(jitter, sizeof(jitter));
            read_index = write_index = 0;
            next_playout = xTaskGetTickCount();
            handshake_started = next_playout;
            taskENTER_CRITICAL();
            health.authenticated = false;
            taskEXIT_CRITICAL();
            if (state.connected) {
                wire.length = sizeof(wire.data);
                if (!app.begin_session(app.context, wire.data, &wire.length) ||
                    !queue_packet(epoch, true, &wire)) fail("session begin");
            }
        }
        /* Absolute deadline: unrelated/control floods cannot extend it. */
        if (state.connected && !authenticated && !session_failed &&
            (TickType_t)(xTaskGetTickCount() - handshake_started) >=
                pdMS_TO_TICKS(AV_HANDSHAKE_TIMEOUT_MS)) {
            session_failed = true;
            app.reset_session(app.context);
            hal.playback_reset(hal.context);
            erase(jitter, sizeof(jitter));
            read_index = write_index = 0;
            increment(&health.rejects);
        }
        /* At most two control/media packets per cycle: bound flood processing.
         * Crypto callbacks must have measured WCET below the audio budget. */
        for (unsigned n = 0; n < 2u; ++n) {
            if (xQueueReceive(incoming_queue, &wire, 0) != pdPASS) break;
            if (!state.connected || wire.epoch != epoch || session_failed) continue;
            if (wire.control) {
                wire_event response;
                bool verified = false;
                response.length = sizeof(response.data);
                if (!app.on_control(app.context, wire.data, wire.length,
                                    response.data, &response.length, &verified)) {
                    increment(&health.rejects);
                    /* Fail closed on handshake errors. Application must only
                     * retry after reset/new epoch; no old-key fallback. */
                    app.reset_session(app.context);
                    authenticated = false;
                    session_failed = true;
                } else {
                    authenticated = verified;
                    if (!queue_packet(epoch, true, &response)) {
                        authenticated = false;
                        session_failed = true;
                        app.reset_session(app.context);
                    }
                }
                if (!authenticated) {
                    erase(jitter, sizeof(jitter));
                    read_index = write_index = 0;
                    hal.playback_reset(hal.context);
                }
            } else if (authenticated) {
                /* Decode into the unoccupied fourth slot. A rejected packet
                 * cannot destroy a valid frame even when the FIFO is full. */
                if (app.decrypt_frame(app.context, wire.data, wire.length,
                                      jitter[write_index])) {
                    unsigned next_write = (write_index + 1u) % AV_JITTER_SLOTS;
                    if (next_write == read_index) {
                        erase(jitter[read_index], sizeof(jitter[read_index]));
                        read_index = (read_index + 1u) % AV_JITTER_SLOTS;
                        increment(&health.drops);
                    }
                    write_index = next_write;
                } else increment(&health.rejects);
            }
        }
        /* Drain one completed frame per iteration. Ownership always returns. */
        if (xQueueReceive(capture_queue, &capture, 0) == pdPASS) {
            const int16_t *pcm = NULL;
            if (!hal.capture_claim(hal.context, capture.id, &pcm) || pcm == NULL)
                fail("capture ownership");
            taskENTER_CRITICAL();
            health.capture_tick = capture.tick;
            health.capture_seen = true;
            taskEXIT_CRITICAL();
            if (authenticated && capture.authenticated && state.connected &&
                capture.epoch == epoch &&
                (TickType_t)(xTaskGetTickCount() - capture.tick) < pdMS_TO_TICKS(AV_FRAME_MS)) {
                wire.length = sizeof(wire.data);
                if (app.encrypt_frame(app.context, pcm, wire.data, &wire.length)) {
                    (void)queue_packet(epoch, false, &wire);
                } else {
                    authenticated = false;
                    session_failed = true;
                    app.reset_session(app.context);
                    erase(jitter, sizeof(jitter));
                    read_index = write_index = 0;
                    hal.playback_reset(hal.context);
                    increment(&health.rejects);
                }
            }
            hal.capture_release(hal.context, capture.id);
        }
        /* Recheck before submission; playback_copy atomically checks physical
         * epoch as well, because physical disconnect can race this snapshot. */
        if (snapshot().epoch != epoch) {
            continue;
        }
        TickType_t elapsed = xTaskGetTickCount() - next_playout;
        const TickType_t period = pdMS_TO_TICKS(AV_FRAME_MS);
        if (elapsed >= period) {
            TickType_t periods = elapsed / period;
            next_playout += periods * period;
            /* Preserve clock phase and discard missed slots instead of
             * playing obsolete frames in a catch-up burst. Bounded by FIFO. */
            while (periods > 1u && read_index != write_index) {
                erase(jitter[read_index], sizeof(jitter[read_index]));
                read_index = (read_index + 1u) % AV_JITTER_SLOTS;
                --periods;
                increment(&health.drops);
            }
            const int16_t *output = silence;
            if (authenticated && read_index != write_index) output = jitter[read_index];
            else if (authenticated) increment(&health.underruns);
            if (!hal.playback_copy(hal.context, epoch, output)) increment(&health.drops);
            if (output != silence) {
                erase(jitter[read_index], sizeof(jitter[read_index]));
                read_index = (read_index + 1u) % AV_JITTER_SLOTS;
            }
        }
        taskENTER_CRITICAL();
        health.authenticated = authenticated;
        taskEXIT_CRITICAL();
        beat(0);
        if (uxQueueMessagesWaiting(incoming_queue) != 0u ||
            uxQueueMessagesWaiting(capture_queue) != 0u) {
            /* A flooded higher-priority media task must let I/O/USB run. */
            vTaskDelay(1);
        } else {
            elapsed = xTaskGetTickCount() - next_playout;
            (void)ulTaskNotifyTake(pdTRUE, elapsed < period ? period - elapsed : 0u);
        }
    }
}

static void bluetooth_task(void *unused)
{
    (void)unused;
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, 0);
        uint32_t epoch;
        bool connected;
        bool backlog = false;
        wire_event wire;
        if (!hal.link_state(hal.context, &epoch, &connected)) fail("link state");
        taskENTER_CRITICAL();
        bool changed = health.epoch != epoch;
        if (changed) health.authenticated = false;
        health.epoch = epoch;
        health.connected = connected;
        taskEXIT_CRITICAL();
        if (changed) xTaskNotifyGive(media_handle);
        if (connected) {
            for (unsigned n = 0; n < 2u; ++n) {
                wire.epoch = epoch;
                wire.length = sizeof(wire.data);
                if (!hal.receive(hal.context, &wire.control, wire.data, &wire.length))
                    fail("Bluetooth receive");
                if (wire.length > sizeof(wire.data)) fail("Bluetooth length");
                if (wire.length == 0u) break;
                if (xQueueSend(incoming_queue, &wire, 0) != pdPASS)
                    increment(&health.drops);
                else xTaskNotifyGive(media_handle);
                backlog = n == 1u;
            }
        }
        for (unsigned n = 0; n < 2u; ++n) {
            if (xQueueReceive(outgoing_queue, &wire, 0) != pdPASS) break;
            if (connected && wire.epoch == epoch &&
                (wire.control || snapshot().authenticated)) {
                /* send() MUST atomically check epoch against physical connection. */
                if (!hal.send(hal.context, epoch, wire.control, wire.data, wire.length))
                    increment(&health.drops);
            }
        }
        beat(1);
        if (backlog || uxQueueMessagesWaiting(outgoing_queue) != 0u)
            vTaskDelay(1); /* Bounded drain under sustained radio flood. */
        else (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(AV_IO_HEARTBEAT_MS));
    }
}

static void usb_reply(const char *line)
{
    (void)hal.usb_write(hal.context, (const uint8_t *)line, strlen(line));
}

static void usb_command(const char *command)
{
    char reply[192];
    if (strcmp(command, "status") == 0) {
        health_state state = snapshot();
        int n = snprintf(reply, sizeof(reply),
            "link=%u authenticated=%u drops=%lu rejects=%lu underruns=%lu\r\n",
            (unsigned)state.connected, (unsigned)state.authenticated,
            (unsigned long)state.drops, (unsigned long)state.rejects,
            (unsigned long)state.underruns);
        if (n > 0 && (size_t)n < sizeof(reply)) usb_reply(reply);
    } else if (strcmp(command, "identity") == 0) {
        static const char hex[] = "0123456789abcdef";
        for (unsigned i = 0; i < sizeof(public_id); ++i) {
            reply[2u * i] = hex[public_id[i] >> 4];
            reply[2u * i + 1u] = hex[public_id[i] & 15u];
        }
        reply[64] = '\r'; reply[65] = '\n'; reply[66] = '\0';
        usb_reply(reply);
    } else if (strcmp(command, "help") == 0) {
        usb_reply("status | identity | help\r\n");
    } else usb_reply("error: unknown command\r\n");
}

static void usb_task(void *unused)
{
    char command[32];
    size_t used = 0;
    bool overflow = false;
    (void)unused;
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, 0);
        uint8_t bytes[32];
        size_t n = hal.usb_read(hal.context, bytes, sizeof(bytes));
        if (n > sizeof(bytes)) fail("USB length");
        for (size_t i = 0; i < n; ++i) {
            if (bytes[i] == '\r') continue;
            if (bytes[i] == '\n') {
                command[used] = '\0';
                if (!overflow) usb_command(command);
                else usb_reply("error: command too long\r\n");
                used = 0; overflow = false;
            } else if (bytes[i] >= 32u && bytes[i] <= 126u && !overflow) {
                if (used + 1u < sizeof(command)) command[used++] = (char)bytes[i];
                else overflow = true;
            } else overflow = true;
        }
        beat(2);
        /* A short read need not mean empty (for example one USB endpoint
         * chunk). Drain until an actual empty read, yielding between chunks. */
        if (n != 0u) vTaskDelay(1);
        else (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(AV_IO_HEARTBEAT_MS));
    }
}

static void supervisor_task(void *unused)
{
    TickType_t start = xTaskGetTickCount();
    (void)unused;
    for (;;) {
        TickType_t now = xTaskGetTickCount();
        health_state state = snapshot();
        bool healthy = !state.fatal_error;
        for (unsigned i = 0; i < 3u; ++i)
            healthy = healthy && (TickType_t)(now - state.heartbeat[i]) < pdMS_TO_TICKS(250);
        /* DMA must produce actual capture completions, even before pairing. */
        if ((TickType_t)(now - start) >= pdMS_TO_TICKS(250))
            healthy = healthy && state.capture_seen &&
                (TickType_t)(now - state.capture_tick) < pdMS_TO_TICKS(100);
        if (!healthy) fail("watchdog health");
        if (!hal.watchdog_feed(hal.context)) fail("watchdog feed");
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

bool av_firmware_capture_from_isr(uint8_t buffer)
{
    BaseType_t wake = pdFALSE;
    capture_event event;
    if (!created) return false;
    event.id = buffer;
    event.tick = xTaskGetTickCountFromISR();
    event.epoch = health.epoch; /* Single core, aligned 32-bit snapshot. */
    event.authenticated = health.authenticated;
    if (xQueueSendFromISR(capture_queue, &event, &wake) != pdPASS) return false;
    vTaskNotifyGiveFromISR(media_handle, &wake);
    portYIELD_FROM_ISR(wake);
    return true;
}

void av_firmware_bluetooth_notify(void)
{
    if (created) xTaskNotifyGive(bluetooth_handle);
}

void av_firmware_bluetooth_notify_from_isr(void)
{
    BaseType_t wake = pdFALSE;
    if (!created) return;
    vTaskNotifyGiveFromISR(bluetooth_handle, &wake);
    portYIELD_FROM_ISR(wake);
}

void av_firmware_usb_notify(void)
{
    if (created) xTaskNotifyGive(usb_handle);
}

void av_firmware_usb_notify_from_isr(void)
{
    BaseType_t wake = pdFALSE;
    if (!created) return;
    vTaskNotifyGiveFromISR(usb_handle, &wake);
    portYIELD_FROM_ISR(wake);
}

bool av_firmware_create(const av_firmware_app_ops *application,
                        const av_firmware_hal_ops *hardware)
{
    if (created || application == NULL || hardware == NULL) return false;
    if (!application->reset_session || !application->begin_session ||
        !application->on_control || !application->encrypt_frame ||
        !application->decrypt_frame || !application->public_identity ||
        !hardware->link_state || !hardware->receive || !hardware->send ||
        !hardware->capture_claim || !hardware->capture_release ||
        !hardware->playback_copy || !hardware->playback_reset ||
        !hardware->usb_read || !hardware->usb_write || !hardware->watchdog_feed ||
        !hardware->fatal) return false;
    app = *application;
    hal = *hardware;
    if (!app.public_identity(app.context, public_id)) return false;
    health = (health_state){0};
    /* Epoch 0 is reserved for disconnected startup. BSP starts at >=1. */
    capture_queue = xQueueCreateStatic(AV_CAPTURE_QUEUE, sizeof(capture_event),
                                     capture_storage, &capture_control);
    incoming_queue = xQueueCreateStatic(AV_FW_QUEUE, sizeof(wire_event),
                                      incoming_storage, &incoming_control);
    outgoing_queue = xQueueCreateStatic(AV_FW_QUEUE, sizeof(wire_event),
                                      outgoing_storage, &outgoing_control);
    configASSERT(capture_queue && incoming_queue && outgoing_queue);
    media_handle = xTaskCreateStatic(media_task, "media", AV_MEDIA_STACK, NULL,
                                         5, media_stack, &task_control[0]);
    configASSERT(media_handle != NULL);
    bluetooth_handle = xTaskCreateStatic(bluetooth_task, "bluetooth", AV_IO_STACK, NULL,
                             4, bt_stack, &task_control[1]);
    configASSERT(bluetooth_handle != NULL);
    usb_handle = xTaskCreateStatic(usb_task, "usb_diag", AV_IO_STACK, NULL,
                             1, usb_stack, &task_control[2]);
    configASSERT(usb_handle != NULL);
    TaskHandle_t task = xTaskCreateStatic(supervisor_task, "supervisor", AV_SUP_STACK, NULL,
                             6, supervisor_stack, &task_control[3]);
    configASSERT(task != NULL);
    created = true;
    return true;
}

void vApplicationGetIdleTaskMemory(StaticTask_t **control, StackType_t **stack,
                                 configSTACK_DEPTH_TYPE *size)
{
    *control = &idle_control;
    *stack = idle_stack;
    *size = configMINIMAL_STACK_SIZE;
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task; (void)name;
    /* Scheduler/stack state may be compromised: no more FreeRTOS calls. */
    health.fatal_error = true;
    hal.fatal(hal.context, "stack overflow");
    for (;;) { /* BSP contract violation; hardware watchdog must reset. */ }
}
