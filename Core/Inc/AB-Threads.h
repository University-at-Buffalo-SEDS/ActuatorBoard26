#pragma once
#include "tx_api.h"

/* ------ Telemetry Thread ------ */
extern TX_THREAD telemetry_thread;

void telemetry_thread_entry(ULONG initial_input);
UINT create_telemetry_thread(TX_BYTE_POOL *byte_pool);
/* ------ Telemetry Thread ------ */

/* ------ Main Task ------ */
extern TX_THREAD main_task_thread;

void main_task_entry(ULONG initial_input);
UINT create_main_task(TX_BYTE_POOL *byte_pool);
/* ------ Main Task ------ */

/* ------ Safety Task ------ */
extern TX_THREAD safety_task_thread;

void safety_task_entry(ULONG initial_input);
UINT create_safety_task(TX_BYTE_POOL *byte_pool);
/* ------ Safety Task ------ */
