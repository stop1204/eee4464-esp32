#include "unity.h"
#include <string.h>
#include <stdbool.h>

// Definition copied from application
typedef struct {
    char endpoint[64];
    char json_body[256];
} http_request_t;

// ---- Mock FreeRTOS queue API ----
typedef struct {
    int capacity;
    int count;
} MockQueue;

typedef MockQueue* QueueHandle_t;
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef int TickType_t;

#define pdTRUE 1
#define pdFALSE 0
#define pdMS_TO_TICKS(x) (x)

static QueueHandle_t http_request_queue = NULL;

static BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t wait)
{
    if (q->count < q->capacity) {
        q->count++;
        return pdTRUE;
    }
    return pdFALSE;
}

static BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t wait)
{
    if (q->count > 0) {
        q->count--;
        return pdTRUE;
    }
    return pdFALSE;
}

static UBaseType_t uxQueueSpacesAvailable(QueueHandle_t q)
{
    return q->capacity - q->count;
}

// Silence logging macros used in original implementation
#define ESP_LOGW(tag, fmt, ...)

// Function under test (copied from main.c)
bool send_to_http_queue(http_request_t* req, int priority, TickType_t wait_ticks)
{
    if (priority > 0) {
        UBaseType_t spaces = uxQueueSpacesAvailable(http_request_queue);
        if (spaces < 3) {
            http_request_t dummy;
            for (int i = 0; i < 5 && spaces < 5; i++) {
                if (xQueueReceive(http_request_queue, &dummy, 0) == pdTRUE) {
                    spaces++;
                } else {
                    break;
                }
            }
        }
        return xQueueSend(http_request_queue, req, 300) == pdTRUE;
    }
    return xQueueSend(http_request_queue, req, wait_ticks) == pdTRUE;
}

// ---------------------- Tests -------------------------
TEST_CASE("High priority request enqueued when queue space is low", "[http_queue]")
{
    MockQueue q = { .capacity = 5, .count = 4 };
    http_request_queue = &q;

    http_request_t req = {0};
    strcpy(req.endpoint, "/api/test");

    bool ok = send_to_http_queue(&req, 10, 0);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT(1, q.count); // old items dropped and request enqueued
}

TEST_CASE("Low priority request fails when queue is full", "[http_queue]")
{
    MockQueue q = { .capacity = 3, .count = 3 };
    http_request_queue = &q;

    http_request_t req = {0};
    strcpy(req.endpoint, "/api/test");

    bool ok = send_to_http_queue(&req, 0, 0);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_INT(3, q.count); // queue unchanged
}

