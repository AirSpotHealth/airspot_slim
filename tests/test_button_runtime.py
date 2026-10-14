"""Execute the actual button driver with deterministic edges and work deadlines."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]

class ButtonRuntimeTests(unittest.TestCase):
    def test_interrupt_debounce_and_hold(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            fake='''#ifndef FAKE_H
#define FAKE_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <errno.h>
#define CONFIG_AIRSPOT_BUTTON_LONG_PRESS_TIMEOUT_MS 5000
#define ARG_UNUSED(x) (void)(x)
#define BIT(x) (1u<<(x))
#define K_MSEC(x) (x)
#define K_SECONDS(x) ((x)*1000)
#define K_PRIO_PREEMPT(x) (x)
#define K_KERNEL_STACK_DEFINE(name,n) char name[n]
#define K_KERNEL_STACK_SIZEOF(name) sizeof(name)
#define K_SEM_DEFINE(name,a,b) int name
#define GPIO_INPUT 0
#define GPIO_INT_EDGE_BOTH 3
#define GPIO_INT_DISABLE 0
#define DT_ALIAS(x) 0
#define GPIO_DT_SPEC_GET(a,b) {.pin=2}
#define LOG_MODULE_REGISTER(...)
#define LOG_ERR(...)
#define LOG_INF(...)
typedef int k_timeout_t;
typedef int32_t atomic_t;
static int32_t atomic_get(atomic_t *v) {return *v;}
static void atomic_set(atomic_t *v,int32_t n) {*v=n;}
static void atomic_clear(atomic_t *v) {*v=0;}
struct device {int unused;};
struct gpio_dt_spec {struct device *port; unsigned pin;};
struct gpio_callback {int unused;};
struct k_work {int unused;};
struct k_work_delayable {struct k_work work; void (*handler)(struct k_work *); int64_t due;};
struct k_work_q {int thread;};
struct k_work_sync {int unused;};
static uint32_t now;
static int level,irq_mode;
static uint32_t k_uptime_get_32(void) {return now;}
static int gpio_pin_get_dt(const struct gpio_dt_spec *p) {return level;}
static bool gpio_is_ready_dt(const struct gpio_dt_spec *p) {return true;}
static int gpio_pin_configure_dt(const struct gpio_dt_spec *p,int flags) {return 0;}
static int gpio_pin_interrupt_configure_dt(const struct gpio_dt_spec *p,int flags) {irq_mode=flags;return 0;}
static void gpio_init_callback(struct gpio_callback *c,void (*f)(const struct device *,struct gpio_callback *,uint32_t),uint32_t mask) {}
static int gpio_add_callback(struct device *p,struct gpio_callback *c) {return 0;}
static void k_work_init_delayable(struct k_work_delayable *w,void (*fn)(struct k_work *)) {w->handler=fn;w->due=-1;}
static int k_work_reschedule_for_queue(struct k_work_q *q,struct k_work_delayable *w,int delay) {w->due=(int64_t)now+delay;return 0;}
static int k_work_cancel_delayable(struct k_work_delayable *w) {w->due=-1;return 0;}
static int k_work_cancel_delayable_sync(struct k_work_delayable *w,struct k_work_sync *s) {w->due=-1;return 0;}
static void k_work_queue_init(struct k_work_q *q) {}
static void k_work_queue_start(struct k_work_q *q,void *s,size_t n,int prio,void *cfg) {}
static void k_thread_name_set(int *thread,const char *name) {}
static void k_sem_give(int *sem) {*sem=1;}
static int k_sem_take(int *sem,int timeout) {if(*sem){*sem=0;return 0;}return -EAGAIN;}
#endif
'''
            for name in ['types.h','device.h','drivers/gpio.h','kernel.h','logging/log.h','sys/atomic.h']:
                p=root/'zephyr'/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(fake)
            source=root/'test.c'
            source.write_text('#include <assert.h>\n#include "'+str(ROOT/'application/src/drivers/button_driver.c')+'"\n'+'''
static int events[128],count;
static void event(button_event_t e) {events[count++]=e;}
static void advance(uint32_t target) {
  for (;;) {
    struct k_work_delayable *w=NULL;
    if(debounce_work.due>=0 && debounce_work.due<=target) w=&debounce_work;
    if(long_press_work.due>=0 && long_press_work.due<=target && (!w || long_press_work.due<w->due)) w=&long_press_work;
    if(!w)break;
    now=w->due;w->due=-1;w->handler(&w->work);
  }
  now=target;
}
static void edge(int value,uint32_t timestamp) {advance(timestamp);level=value;edge_handler(NULL,NULL,4);}
int main(void) {
  assert(button_driver_init()==0 && irq_mode==GPIO_INT_EDGE_BOTH);
  button_driver_register_callback(event);
  advance(30);assert(count==0 && debounce_work.due==-1);
  edge(1,100);edge(0,105);edge(1,110);
  advance(129);assert(count==0);
  advance(130);assert(count==1 && events[0]==BUTTON_EVENT_PRESSED);
  edge(0,200);advance(220);assert(count==2 && events[1]==BUTTON_EVENT_RELEASED);
  advance(6000);assert(count==2 && debounce_work.due==-1 && long_press_work.due==-1);
  edge(1,6100);advance(11120);assert(count==4 && events[3]==BUTTON_EVENT_LONG_PRESS);
  advance(16000);assert(count==4); /* one long press per hold */
  edge(0,16050);advance(16070);assert(count==5);
  /* A held wake button must not cause a second off gesture. */
  level=1;atomic_set(&button_pressed,1);button_driver_register_callback(event);
  advance(22000);assert(count==5);
  edge(0,22100);advance(22120);assert(count==5);
  edge(1,22200);advance(22220);assert(count==6);
  level=-EIO;advance(27220);assert(count==6); /* no false long press on GPIO error */
  level=0;edge(0,28000);advance(28020);
  int base=count;
  edge(1,30000);advance(40020);
  assert(events[count-1]==BUTTON_EVENT_RECOVERY_WARNING);
  advance(45020);assert(events[count-1]==BUTTON_EVENT_RECOVERY_READY);
  edge(0,46000);advance(46020);
  assert(events[count-2]==BUTTON_EVENT_RECOVERY_CONFIRMED);
  assert(events[count-1]==BUTTON_EVENT_RELEASED);
  edge(1,50000);advance(68020);
  assert(events[count-1]==BUTTON_EVENT_RECOVERY_CANCELLED);
  int trapped=count;advance(71000);assert(count==trapped);
  edge(0,71000);advance(71020);
  assert(events[count-2]==BUTTON_EVENT_RECOVERY_CANCELLED);
  edge(1,72000);advance(87020);edge(0,87999);advance(88019);
  assert(events[count-2]==BUTTON_EVENT_RECOVERY_CONFIRMED);
  assert(button_driver_prepare_wakeup()==0 && irq_mode==GPIO_INT_DISABLE);
  int final_count=count;edge(0,90000);advance(91000);assert(count==final_count && debounce_work.due==-1);
}
''')
            subprocess.run(['cc','-std=c11','-fsanitize=address,undefined','-I',str(root),'-I',str(ROOT/'application/include'),str(source),'-o',str(root/'test')],check=True)
            subprocess.run([str(root/'test')],check=True)
