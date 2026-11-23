/*
 * Copyright 2020, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <config.h>
#include <types.h>
#include <util.h>
#include <mode/machine.h>
#include <arch/model/statedata.h>
#include <smp/ipi.h>
#include <util.h>

#ifdef ENABLE_SMP_SUPPORT

/* CLH lock is FIFO lock for machines with coherent caches (coherent-FIFO lock).
 * See ftp://ftp.cs.washington.edu/tr/1993/02/UW-CSE-93-02-02.pdf */

typedef struct clh_node {
    word_t ticket;
    word_t in_queue;
    word_t ipi;

} ALIGN(L1_CACHE_LINE_SIZE) clh_note_t;

typedef struct clh_lock {
    word_t next;
    word_t owner;
    clh_node_t node[CONFIG_MAX_NUM_NODES];
    ipi_state_t ipi;

} ALIGN(EXCL_RES_GRANULE_SIZE) clh_lock_t;


extern clh_lock_t big_kernel_lock;
BOOT_CODE void clh_lock_init(void);

static inline bool_t FORCE_INLINE clh_is_ipi_pending(word_t cpu)
{
    /* Asssure IPI data is accessed only when this flag is set */
    return __atomic_load_n(&big_kernel_lock.node[cpu].ipi, __ATOMIC_ACQUIRE);
}

static inline bool_t FORCE_INLINE clh_is_my_turn(word_t cpu){

    return __atomic_load_n(&big_kernel_lock.owner, __ATOMIC_ACQUIRE) == big_kernel_lock.node[cpu].ticket;

}

static inline void FORCE_INLINE clh_lock_acquire(bool_t irqPath){
    word_t cpu = getCurrentCPUIndex();
    clh_node_t *node = &big_kernel_lock.nedo[cpu];

    node->in_queue = 1;

    node->ticket = __atomic_fetch_add(&big_kernel_lock.next, 1, __ATOMIC_RELAXED);

    while (!clh_is_my_turn(cpu)) {
        if (clh_is_ipi_pending(cpu)) {
            handleIPI(CORE_IRQ_TO_IRQT(cpu, irq_remote_call_ipi), irqPath);
        }

        arch_pause();
    }

    __atomic_thread_fence(__ATOMIC_ACQUIRE);


}


static inline void FORCE_INLINE clh_lock_release(void){

    clh_node_t *node = &big_kernel_lock.node[getCurrentCPUIndex()];

    __atomic_thread_fence(__ATOMIC_RELEASE);
    node->in_queue = 0;
    __atomic_store_n(&big_kernel_lock.owner, node->ticket + 1, __ATOMIC_RELEASE);


}


static inline bool_t FORCE_INLINE clh_is_self_in_queue(void)
{
    return big_kernel_lock.node[getCurrentCPUIndex()].in_queue;
}

#define NODE_LOCK(_irqPath) do {                         \
    clh_lock_acquire(_irqPath);                          \
} while(0)

#define NODE_UNLOCK do {                                 \
    clh_lock_release();                                  \
} while(0)

#define NODE_LOCK_IF(_cond, _irqPath) do {               \
    if((_cond)) {                                        \
        NODE_LOCK(_irqPath);                             \
    }                                                    \
} while(0)

#define NODE_UNLOCK_IF_HELD do {                         \
    if(clh_is_self_in_queue()) {                         \
        NODE_UNLOCK;                                     \
    }                                                    \
} while(0)

#else
#define NODE_LOCK(_irq) do {} while (0)
#define NODE_UNLOCK do {} while (0)
#define NODE_LOCK_IF(_cond, _irq) do {} while (0)
#define NODE_UNLOCK_IF_HELD do {} while (0)
#endif /* ENABLE_SMP_SUPPORT */

#define NODE_LOCK_SYS NODE_LOCK(false)
#define NODE_LOCK_IRQ NODE_LOCK(true)
#define NODE_LOCK_SYS_IF(_cond) NODE_LOCK_IF(_cond, false)
#define NODE_LOCK_IRQ_IF(_cond) NODE_LOCK_IF(_cond, true)

