#ifndef _H_RING2
#define _H_RING2

typedef struct Ring RingItem;

typedef struct Ring {
	RingItem* prev;
	RingItem* next;
} Ring;

static inline void ring_init(Ring* ring)
{
	ring->next = ring->prev = ring;
}

static inline void ring_item_init(RingItem* item)
{
	item->next = item->prev = 0;
}

static inline int ring_item_is_linked(RingItem* item)
{
	return !!item->next;
}

static inline int ring_is_empty(Ring* ring)
{
	if (ring->next != 0 && ring->prev != 0) {
		return ring == ring->next;
	}
	return 0;
}

static inline void ring_add(Ring* ring, RingItem* item)
{
	if (!(ring->next != 0 && ring->prev != 0)) {
		return;
	}
	if (!(item->next == 0 && item->prev == 0)) {
		return;
	}

	item->next = ring->next;
	item->prev = ring;
	ring->next = item->next->prev = item;
}

static inline void ring_add_after(RingItem* item, RingItem* pos)
{
	ring_add(pos, item);
}

static inline void ring_add_before(RingItem* item, RingItem* pos)
{
	ring_add(pos->prev, item);
}

static inline void ring_remove(RingItem* item)
{
	if (!(item->next != 0 && item->prev != 0)) {
		return;
	}
	if (!(item->next != item)) {
		return;
	}

	item->next->prev = item->prev;
	item->prev->next = item->next;
	item->prev = item->next = 0;
}

static inline RingItem* ring_get_head(Ring* ring)
{
	if (!(ring->next != 0 && ring->prev != 0)) {
		return 0;
	}

	if (ring_is_empty(ring)) {
		return 0;
	}
	return ring->next;
}

static inline RingItem* ring_get_tail(Ring* ring)
{
	if (!(ring->next != 0 && ring->prev != 0)) {
		return 0;
	}

	if (ring_is_empty(ring)) {
		return 0;
	}
	return ring->prev;
}

static inline RingItem* ring_next(Ring* ring, RingItem* pos)
{
	RingItem* ret;

	if (!(ring->next != 0 && ring->prev != 0)) {
		return 0;
	}
	if (!pos) {
		return 0;
	}
	if (!(pos->next != 0 && pos->prev != 0)) {
		return 0;
	}
	ret = pos->next;
	return (ret == ring) ? 0 : ret;
}

static inline RingItem* ring_prev(Ring* ring, RingItem* pos)
{
	RingItem* ret;

	if (!(ring->next != 0 && ring->prev != 0)) {
		return 0;
	}
	if (!pos) {
		return 0;
	}
	if (!(pos->next != 0 && pos->prev != 0)) {
		return 0;
	}
	ret = pos->prev;
	return (ret == ring) ? 0 : ret;
}

#define RING_FOREACH_SAFE(var, next, ring)                    \
    for ((var) = ring_get_head(ring);                         \
            (var) && ((next) = ring_next(ring, (var)), 1);    \
            (var) = (next))


#define RING_FOREACH(var, ring)                 \
    for ((var) = ring_get_head(ring);           \
            (var);                              \
            (var) = ring_next(ring, var))

#define RING_FOREACH_REVERSED(var, ring)        \
    for ((var) = ring_get_tail(ring);           \
            (var);                              \
            (var) = ring_prev(ring, var))


static inline unsigned int ring_get_length(Ring* ring)
{
	RingItem* i;
	unsigned int ret = 0;

	RING_FOREACH(i, ring)
		ret++;

	return ret;
}


#endif
