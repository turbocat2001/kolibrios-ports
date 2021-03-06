/*
 * kernel/workqueue.c - generic async execution with shared worker pool
 *
 * Copyright (C) 2002		Ingo Molnar
 *
 *   Derived from the taskqueue/keventd code by:
 *     David Woodhouse <dwmw2@infradead.org>
 *     Andrew Morton
 *     Kai Petzke <wpp@marie.physik.tu-berlin.de>
 *     Theodore Ts'o <tytso@mit.edu>
 *
 * Made to use alloc_percpu by Christoph Lameter.
 *
 * Copyright (C) 2010		SUSE Linux Products GmbH
 * Copyright (C) 2010		Tejun Heo <tj@kernel.org>
 *
 * This is the generic async execution mechanism.  Work items as are
 * executed in process context.  The worker pool is shared and
 * automatically managed.  There are two worker pools for each CPU (one for
 * normal work items and the other for high priority ones) and some extra
 * pools for workqueues which are not bound to any specific CPU - the
 * number of these backing pools is dynamic.
 *
 * Please read Documentation/workqueue.txt for details.
 */

#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/completion.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/lockdep.h>
#include <linux/idr.h>

extern int driver_wq_state;

struct workqueue_struct *alloc_workqueue(const char *fmt,
                           unsigned int flags,
                           int max_active)
{
    struct workqueue_struct *wq;

    wq = kzalloc(sizeof(*wq),0);
    if (!wq)
        goto err;

    INIT_LIST_HEAD(&wq->worklist);
    INIT_LIST_HEAD(&wq->delayed_worklist);

    return wq;
err:
    return NULL;
}



void run_workqueue(struct workqueue_struct *cwq)
{
    unsigned long irqflags;

//    dbgprintf("wq: %x head %x, next %x\n",
//               cwq, &cwq->worklist, cwq->worklist.next);

    while(driver_wq_state != 0)
    {
        spin_lock_irqsave(&cwq->lock, irqflags);

        while (!list_empty(&cwq->worklist))
        {
            struct work_struct *work = list_entry(cwq->worklist.next,
                                        struct work_struct, entry);
            work_func_t f = work->func;
            list_del_init(cwq->worklist.next);
//            printf("work %p, func %p\n",
//                      work, f);

            spin_unlock_irqrestore(&cwq->lock, irqflags);
            f(work);
            spin_lock_irqsave(&cwq->lock, irqflags);
        }

        spin_unlock_irqrestore(&cwq->lock, irqflags);

        kos_delay(1);
    };
}


bool queue_work(struct workqueue_struct *wq, struct work_struct *work)
{
    unsigned long flags;

    if(!list_empty(&work->entry))
        return 0;

//    dbgprintf("%s %p queue: %p\n", __FUNCTION__, work, wq);

    spin_lock_irqsave(&wq->lock, flags);

    list_add_tail(&work->entry, &wq->worklist);

    spin_unlock_irqrestore(&wq->lock, flags);

    return 1;
};


void __stdcall delayed_work_timer_fn(unsigned long __data)
{
    struct delayed_work *dwork = (struct delayed_work *)__data;
    struct workqueue_struct *wq = dwork->work.data;

    queue_work(wq, &dwork->work);
}

bool queue_delayed_work(struct workqueue_struct *wq,
                        struct delayed_work *dwork, unsigned long delay)
{
    struct work_struct *work = &dwork->work;

    if (delay == 0)
        return queue_work(wq, &dwork->work);

//    dbgprintf("%s %p queue: %p\n", __FUNCTION__, &dwork->work, wq);

    work->data = wq;
    kos_timer_hs(delay,0, delayed_work_timer_fn, dwork);
    return 1;
}


bool schedule_delayed_work(struct delayed_work *dwork, unsigned long delay)
{
    return queue_delayed_work(system_wq, dwork, delay);
}

//bool mod_delayed_work(struct workqueue_struct *wq,
//                                    struct delayed_work *dwork,
//                                    unsigned long delay)
//{
//    return queue_delayed_work(wq, dwork, delay);
//}

int del_timer(struct timer_list *timer)
{
    int ret = 0;

    if(timer->handle)
    {
        kos_cancel_timer_hs(timer->handle);
        timer->handle = 0;
        ret = 1;
    };
    return ret;
};

bool cancel_work_sync(struct work_struct *work)
{
    unsigned long flags;
    int ret = 0;

    spin_lock_irqsave(&system_wq->lock, flags);
    if(!list_empty(&work->entry))
    {
        list_del(&work->entry);
        ret = 1;
    };
    spin_unlock_irqrestore(&system_wq->lock, flags);
    return ret;
}

bool cancel_delayed_work(struct delayed_work *dwork)
{
    return cancel_work_sync(&dwork->work);
}

bool cancel_delayed_work_sync(struct delayed_work *dwork)
{
    return cancel_work_sync(&dwork->work);
}

int mod_timer(struct timer_list *timer, unsigned long expires)
{
    int ret = 0;
    expires - kos_get_timer_ticks();

    if(timer->handle)
    {
        kos_cancel_timer_hs(timer->handle);
        timer->handle = 0;
        ret = 1;
    };

    timer->handle = kos_timer_hs(expires, 0, timer->function, (void*)timer->data);

    return ret;
}

