#include "fio.h"
#include "mutex.h"
#include "smalloc.h"
#include "flist.h"

struct fio_flow {
	unsigned int refs;
	struct flist_head list;
	unsigned int id;
	long long int flow_counter;
};

static struct flist_head *flow_list;
static struct fio_mutex *flow_lock;

int flow_threshold_exceeded(struct thread_data *td)
{
	struct fio_flow *flow = td->flow;
	int sign;

	if (!flow)
		return 0;

	sign = td->o.flow > 0 ? 1 : -1;
	if (sign * flow->flow_counter > td->o.flow_watermark) {
		if (td->o.flow_sleep)
			usleep(td->o.flow_sleep);
		return 1;
	}

	/* No synchronization needed because it doesn't
	 * matter if the flow count is slightly inaccurate */
	flow->flow_counter += td->o.flow;
	return 0;
}

static struct fio_flow *flow_get(unsigned int id)
{
	struct fio_flow *flow = NULL;
	struct flist_head *n;

	fio_mutex_down(flow_lock);

	flist_for_each(n, flow_list) {
		flow = flist_entry(n, struct fio_flow, list);
		if (flow->id == id)
			break;

		flow = NULL;
	}

	if (!flow) {
		flow = smalloc(sizeof(*flow));
		flow->refs = 0;
		INIT_FLIST_HEAD(&flow->list);
		flow->id = id;
		flow->flow_counter = 0;

		flist_add_tail(&flow->list, flow_list);
	}

	flow->refs++;
	fio_mutex_up(flow_lock);
	return flow;
}

static void flow_put(struct fio_flow *flow)
{
	fio_mutex_down(flow_lock);

	if (!--flow->refs) {
		flist_del(&flow->list);
		sfree(flow);
	}

	fio_mutex_up(flow_lock);
}

void flow_init_job(struct thread_data *td)
{
	if (td->o.flow)
		td->flow = flow_get(td->o.flow_id);
}

void flow_exit_job(struct thread_data *td)
{
	if (td->flow) {
		flow_put(td->flow);
		td->flow = NULL;
	}
}

void flow_init(void)
{
	flow_lock = fio_mutex_init(1);
	flow_list = smalloc(sizeof(*flow_list));
	INIT_FLIST_HEAD(flow_list);
}

void flow_exit(void)
{
	fio_mutex_remove(flow_lock);
	sfree(flow_list);
}
