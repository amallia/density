
#include "queue_generic_tests.h"

namespace density_tests
{
	void concurr_heter_seq_cst_queue_generic_tests(QueueTesterFlags i_flags, std::ostream & i_output,
		EasyRandom & i_rand, size_t i_element_count)
	{
		using namespace density;

		std::vector<size_t> const nonblocking_thread_counts{ 1, 2, 32 * 1024 };

		constexpr auto mult = density::concurrency_multiple;
		constexpr auto single = density::concurrency_single;
		constexpr auto seq_cst = density::consistency_sequential;

		detail::nb_queues_generic_tests<mult, mult, seq_cst>
			(i_flags, i_output, i_rand, i_element_count, nonblocking_thread_counts);

		detail::nb_queues_generic_tests<mult, single, seq_cst>
			(i_flags, i_output, i_rand, i_element_count, nonblocking_thread_counts);

		detail::nb_queues_generic_tests<single, mult, seq_cst>
			(i_flags, i_output, i_rand, i_element_count, nonblocking_thread_counts);

		detail::nb_queues_generic_tests<single, single, seq_cst>
			(i_flags, i_output, i_rand, i_element_count, nonblocking_thread_counts);
	}
}