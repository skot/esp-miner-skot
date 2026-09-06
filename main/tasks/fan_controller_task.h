#ifndef FAN_CONTROLLER_TASK_H_
#define FAN_CONTROLLER_TASK_H_

void FAN_CONTROLLER_task(void * pvParameters);
// ASIC warmup override; refresh within five seconds. Negative disables it.
void FAN_CONTROLLER_set_startup_percent(int percent);

#endif /* FAN_CONTROLLER_TASK_H_ */
