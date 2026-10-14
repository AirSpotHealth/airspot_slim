/* Included after the actual sensor manager and injected hardware harness. */
static void observed(const sensor_data_t *d) {callback_valid=d->valid;callbacks++;}
static void tick_after(int ms) {now+=ms;recovery_work_handler(NULL);}
int main(void) {
  sensor_recovery_diagnostic_t d;
  assert(sensor_manager_init()==0);
  sensor_manager_register_callback(observed);
  power=3;
  assert(sensor_manager_set_power_mode(power)==0);
  assert(sensor_manager_measure_now()==0);
  sensor_manager_start_recovery();
  correction=165;
  uint8_t policy=native_mode;
  int factory_before=factory_resets;

  /* Observed case: worker continues but reads fail for >30 seconds. */
  read_error=-EIO;assert(sensor_manager_measure_now()==-EIO);
  tick_after(29000);assert(recovery_diagnostic.total_attempts==0);
  manual=true;tick_after(5000);assert(recovery_diagnostic.total_attempts==0);
  manual=false;heal_co2=true;tick_after(5000);
  assert(recovery_diagnostic.total_attempts==1 && recovery_diagnostic.stage==2);
  assert(latest_data.valid && continuous && !use_single_shot_mode && running);
  assert(correction==165 && native_mode==policy && factory_resets==factory_before);
  assert(!manual && recovery_diagnostic.consecutive_attempts==0);
  assert(internal_paused==(policy!=2));

  /* Ambient and pressure resets are targeted: don't reset CO2/native history. */
  int resets_before=reset_calls;
  ambient_error=-EIO;heal_ambient=true;
  assert(sensor_manager_measure_now()==-EIO);tick_after(30000);
  assert(latest_data.valid && ambient_resets==1 && reset_calls==resets_before);
  pressure_error=-EIO;heal_pressure=true;
  assert(sensor_manager_measure_now()==-EIO);tick_after(30000);
  assert(latest_data.valid && pressure_resets==1 && reset_calls==resets_before);

  /* Permanent CO2 failure: attempt count advances only after the backoff. */
  heal_co2=false;read_error=-EIO;
  assert(sensor_manager_measure_now()==-EIO);tick_after(30000);
  assert(!latest_data.valid && recovery_diagnostic.stage==3);
  sensor_manager_get_recovery_diagnostic(&d);
  assert(d.consecutive_attempts==1 && d.retry_in_seconds==30 && d.error);
  unsigned total=d.total_attempts;
  tick_after(29000);assert(recovery_diagnostic.total_attempts==total);
  tick_after(1000);sensor_manager_get_recovery_diagnostic(&d);
  assert(d.consecutive_attempts==2 && d.retry_in_seconds==60);
  tick_after(60000);sensor_manager_get_recovery_diagnostic(&d);
  assert(d.consecutive_attempts==3 && d.retry_in_seconds==120);
  tick_after(120000);sensor_manager_get_recovery_diagnostic(&d);
  assert(d.consecutive_attempts==4 && d.retry_in_seconds==300);
  /* Startup reinitialization must keep the worker configured only once. */
  read_error=0;tick_after(300000);
  assert(initialized && latest_data.valid && continuous && running);
  assert(!recovery_diagnostic.consecutive_attempts);

  /* Successful ordinary read before the threshold cancels transient recovery. */
  total=recovery_diagnostic.total_attempts;
  read_error=-EIO;assert(sensor_manager_measure_now()==-EIO);
  tick_after(10000);read_error=0;assert(sensor_manager_measure_now()==0);
  tick_after(30000);assert(recovery_diagnostic.total_attempts==total);

  /* Failed startup, including on-demand mode, remains recoverable. */
  power=0;assert(sensor_manager_set_power_mode(0)==0);
  initialized=false;ambient_error=-EIO;
  assert(sensor_manager_init()==-EIO);
  ambient_error=0;tick_after(30000);
  assert(initialized && latest_data.valid && !running && use_single_shot_mode);
  assert(measurement_interval_ms==0);
  read_error=-EIO;heal_co2=true;
  assert(sensor_manager_measure_now()==-EIO);tick_after(30000);
  assert(latest_data.valid && !running && use_single_shot_mode);
  assert(correction==165 && native_mode==policy && factory_resets==factory_before);

  /* No recovery can resurrect a stopped device. */
  read_error=-EIO;assert(sensor_manager_measure_now()==-EIO);
  total=recovery_diagnostic.total_attempts;
  sensor_manager_stop();tick_after(600000);
  assert(!recovery_enabled && !running && recovery_diagnostic.total_attempts==total);
  assert(meas_mutex==0 && data_mutex==0 && mode_change_mutex==0 && !manual);
}
