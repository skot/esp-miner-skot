#ifndef FAN_CONTROLLER_TASK_H_
#define FAN_CONTROLLER_TASK_H_

void FAN_CONTROLLER_task(void * pvParameters);
// Diagnostic lease; refresh within five seconds. Negative disables it.
void FAN_CONTROLLER_set_bench_percent(int percent);

#endif /* FAN_CONTROLLER_TASK_H_ */
