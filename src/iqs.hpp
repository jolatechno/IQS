#pragma once

#include <parallel/algorithm>
#include <parallel/numeric>

#include <cstddef>
#include <vector>
#include <tbb/concurrent_hash_map.h> // For concurrent hash map.

#ifndef PROBA_TYPE
	#define PROBA_TYPE double
#endif
#ifndef TOLERANCE
	#define TOLERANCE 1e-18
#endif
#ifndef SAFETY_MARGIN
	#define SAFETY_MARGIN 0.2
#endif
#ifndef COLLISION_TEST_PROPORTION
	#define COLLISION_TEST_PROPORTION 0.1
#endif
#ifndef COLLISION_TOLERANCE
	#define COLLISION_TOLERANCE 0.05
#endif

/*
defining openmp function's return values if openmp isn't installed or loaded
*/ 
#ifndef _OPENMP
	#define omp_set_nested(i)
	#define omp_get_thread_num() 0
	#define omp_get_num_thread() 1
#else
	#include <omp.h>
#endif

namespace iqs {
	namespace utils {
		#include "utils/complex.hpp"
		#include "utils/load_balancing.hpp"
		#include "utils/memory.hpp"
		#include "utils/random.hpp"
		#include "utils/vector.hpp"
	}

	/*
	global variable definition
	*/
	namespace {
		PROBA_TYPE tolerance = TOLERANCE;
		float safety_margin = SAFETY_MARGIN;
		float collision_test_proportion = COLLISION_TEST_PROPORTION;
		float collision_tolerance = COLLISION_TOLERANCE;
	}
	
	/*
	global variable setters
	*/
	void set_tolerance(PROBA_TYPE val) { tolerance = val; }
	void set_safety_margin(float val) { safety_margin = val; }
	void set_collision_test_proportion(float val) { collision_test_proportion = val; }
	void set_collision_tolerance(float val) { collision_tolerance = val; }

	/*
	number of threads
	*/
	const size_t num_threads = []() {
		/* get num thread */
		int num_threads;
		#pragma omp parallel
		#pragma omp single
		num_threads = omp_get_num_threads();

		return num_threads;
	}();

	/* forward typedef */
	typedef class iteration it_t;
	typedef class symbolic_iteration sy_it_t;
	typedef class rule rule_t;
	typedef std::function<void(char* parent_begin, char* parent_end, PROBA_TYPE &real, PROBA_TYPE &imag)> modifier_t;
	typedef std::function<void(int step)> debug_t;

	/* 
	rule virtual class
	*/
	class rule {
	public:
		rule() {};
		virtual inline void get_num_child(char const *parent_begin, char const *parent_end, uint32_t &num_child, size_t &max_child_size) const = 0;
		virtual inline char* populate_child(char const *parent_begin, char const *parent_end, uint32_t child_id, PROBA_TYPE &real, PROBA_TYPE &imag, char* child_begin) const = 0;
		virtual inline size_t hasher(char const *parent_begin, char const *parent_end) const { //can be overwritten
			return std::hash<std::string_view>()(std::string_view(parent_begin, std::distance(parent_begin, parent_end)));
		}
	};

	/*
	iteration class
	*/
	class iteration {
		friend symbolic_iteration;
		friend long long int inline get_max_num_object(it_t const &next_iteration, it_t const &last_iteration, sy_it_t const &symbolic_iteration);
		friend void inline simulate(it_t &iteration, rule_t const *rule, it_t &iteration_buffer, sy_it_t &symbolic_iteration, debug_t mid_step_function);  
		friend void inline simulate(it_t &iteration, modifier_t const rule);

	protected:
		utils::numa_vector<PROBA_TYPE> real, imag;
		utils::numa_vector<char> objects;
		utils::numa_vector<size_t> object_begin;
		mutable utils::numa_vector<uint32_t> num_childs;

		void inline resize(size_t num_object) const {
			real.resize(num_object);
			imag.resize(num_object);
			num_childs.resize(num_object + 1);
			object_begin.resize(num_object + 1);
		}
		void inline allocate(size_t size) const {
			objects.resize(size);
		}

		void generate_symbolic_iteration(rule_t const *rule, sy_it_t &symbolic_iteration, debug_t mid_step_function) const;
		void apply_modifier(modifier_t const rule);
		void normalize();

	public:
		size_t num_object = 0;
		PROBA_TYPE total_proba = 1;

		iteration() {
			resize(0);
			allocate(0);
			object_begin[0] = 0;
			num_childs[0] = 0;
		}
		iteration(char* object_begin_, char* object_end_) : iteration() {
			append(object_begin_, object_end_);
		}
		void append(char* object_begin_, char* object_end_, PROBA_TYPE real_ = 1, PROBA_TYPE imag_ = 0) {
			size_t offset = object_begin[num_object];
			size_t size = std::distance(object_begin_, object_end_);

			resize(++num_object);
			allocate(offset + size);

			for (size_t i = 0; i < size; ++i)
				objects[offset + i] = object_begin_[i];

			real[num_object - 1] = real_; imag[num_object - 1] = imag_;
			object_begin[num_object] = offset + size;
		}
		char* get_object(size_t object_id, size_t &object_size, PROBA_TYPE *&real_, PROBA_TYPE *&imag_) {
			size_t this_object_begin = object_begin[object_id];
			object_size = object_begin[object_id + 1] - this_object_begin;
			real_ = &real[object_id]; imag_ = &imag[object_id];
			return objects.begin() + this_object_begin;
		}
		char const* get_object(size_t object_id, size_t &object_size, PROBA_TYPE &real_, PROBA_TYPE &imag_) const {
			size_t this_object_begin = object_begin[object_id];
			object_size = object_begin[object_id + 1] - this_object_begin;
			real_ = real[object_id]; imag_ = imag[object_id];
			return objects.begin() + this_object_begin;
		}
	};

	/*
	symboluc iteration class
	*/
	class symbolic_iteration {
		friend iteration;
		friend long long int inline get_max_num_object(it_t const &next_iteration, it_t const &last_iteration, sy_it_t const &symbolic_iteration);
		friend void inline simulate(it_t &iteration, rule_t const *rule, it_t &iteration_buffer, sy_it_t &symbolic_iteration, debug_t mid_step_function); 

	protected:
		tbb::concurrent_hash_map<size_t, size_t> elimination_map;
		std::vector<char*> placeholder = std::vector<char*>(num_threads, NULL);

		utils::numa_vector<PROBA_TYPE> real, imag;
		utils::numa_vector<size_t> next_oid;
		utils::numa_vector<size_t> size;
		utils::numa_vector<size_t> hash;
		utils::numa_vector<size_t> parent_oid;
		utils::numa_vector<uint32_t> child_id;
		utils::numa_vector<bool> is_unique;
		utils::numa_vector<double> random_selector;

		void inline resize(size_t num_object) {
			real.resize(num_object);
			imag.resize(num_object);
			next_oid.iota_resize(num_object);
			size.zero_resize(num_object);
			hash.zero_resize(num_object);
			parent_oid.resize(num_object);
			child_id.resize(num_object);
			is_unique.resize(num_object);
			random_selector.zero_resize(num_object);
		}
		void inline reserve(size_t max_size) {
			#pragma omp parallel
			{
				auto &buffer = placeholder[omp_get_thread_num()];
				if (buffer == NULL)
					free(buffer);
				buffer = new char[max_size];
				for (size_t i = 0; i < max_size; ++i) buffer[i] = 0; // touch
			}
		}

		void compute_collisions();
		void finalize(rule_t const *rule, it_t const &last_iteration, it_t &next_iteration, debug_t mid_step_function);

	public:
		size_t num_object = 0;
		size_t num_object_after_interferences = 0;

		symbolic_iteration() {}
	};

	/*
	for memory managment
	*/
	long long int inline get_max_num_object(it_t const &next_iteration, it_t const &last_iteration, sy_it_t const &symbolic_iteration) {
		static long long int iteration_size = 2*sizeof(PROBA_TYPE) + sizeof(size_t) + sizeof(uint32_t);
		static long long int symbolic_iteration_size = 1 + 2*sizeof(PROBA_TYPE) + 6*sizeof(size_t) + sizeof(uint32_t) + sizeof(double);

		// get the free memory and the total amount of memory...
		long long int total_memory, free_mem;
		utils::get_mem_usage_and_free_mem(total_memory, free_mem);

		// and according to the "safety_margin" (a proportion of total memory) compute the total delta between the amount free memory and the target
		long long int mem_difference = free_mem - total_memory*safety_margin;

		// get the total memory
		long long int total_useable_memory = next_iteration.objects.size() + last_iteration.objects.size() + // size of objects
			(last_iteration.real.size() + next_iteration.real.size())*iteration_size + // size of properties
			symbolic_iteration.real.size()*symbolic_iteration_size + // size of symbolic properties
			mem_difference; // free memory

		// compute average object size
		long long int iteration_size_per_object = 0;

		// compute the average size of an object for the next iteration:
		#pragma omp parallel for reduction(+:iteration_size_per_object)
		for (size_t oid = 0; oid < symbolic_iteration.num_object_after_interferences; ++oid)
			iteration_size_per_object += symbolic_iteration.size[oid];
		iteration_size_per_object /= symbolic_iteration.num_object_after_interferences;

		// add the cost of the symbolic iteration in itself
		iteration_size_per_object += symbolic_iteration_size*symbolic_iteration.num_object/last_iteration.num_object/2; // size for symbolic iteration
		
		// and the constant size per object
		iteration_size_per_object += iteration_size;

		// and the cost of unused space
		iteration_size_per_object *= utils::upsize_policy;

		return total_useable_memory / iteration_size_per_object;
	}

	/*
	simulation function
	*/
	void inline simulate(it_t &iteration, rule_t const *rule, it_t &iteration_buffer, sy_it_t &symbolic_iteration, debug_t mid_step_function=[](int){}) {
		iteration.generate_symbolic_iteration(rule, symbolic_iteration, mid_step_function);
		symbolic_iteration.compute_collisions();
		symbolic_iteration.finalize(rule, iteration, iteration_buffer, mid_step_function);
		iteration_buffer.normalize();

		mid_step_function(8);
		
		std::swap(iteration_buffer, iteration);
	}
	void inline simulate(it_t &iteration, modifier_t const rule) {
		iteration.apply_modifier(rule);
	}

	/*
	apply modifier
	*/
	void iteration::apply_modifier(modifier_t const rule) {
		#pragma omp parallel for schedule(static)
		for (size_t oid = 0; oid < num_object; ++oid)
			/* generate graph */
			rule(objects.begin() + object_begin[oid],
				objects.begin() + object_begin[oid + 1],
				real[oid], imag[oid]);
	}

	/*
	generate symbolic iteration
	*/
	void iteration::generate_symbolic_iteration(rule_t const *rule, sy_it_t &symbolic_iteration, debug_t mid_step_function=[](int){}) const {
		if (num_object == 0) {
			symbolic_iteration.num_object = 0;
			return;
		}

		size_t max_size;

		mid_step_function(0);

		/* !!!!!!!!!!!!!!!!
		step (1)
		 !!!!!!!!!!!!!!!! */

		#pragma omp parallel for schedule(static) reduction(max:max_size)
		for (size_t oid = 0; oid < num_object; ++oid) {
			size_t size;
			rule->get_num_child(objects.begin() + object_begin[oid],
				objects.begin() + object_begin[oid + 1],
				num_childs[oid + 1], size);
			max_size = std::max(max_size, size);
		}

		mid_step_function(1);

		/* !!!!!!!!!!!!!!!!
		step (2)
		 !!!!!!!!!!!!!!!! */

		__gnu_parallel::partial_sum(num_childs.begin() + 1, num_childs.begin() + num_object + 1, num_childs.begin() + 1);
		symbolic_iteration.num_object = num_childs[num_object];

		/* resize symbolic iteration */
		symbolic_iteration.resize(symbolic_iteration.num_object);
		symbolic_iteration.reserve(max_size);
		
		#pragma omp parallel
		{
			auto thread_id = omp_get_thread_num();

			#pragma omp for schedule(static)
			for (size_t oid = 0; oid < num_object; ++oid) {
				/* assign parent ids and child ids for each child */
				std::fill(symbolic_iteration.parent_oid.begin() + num_childs[oid],
					symbolic_iteration.parent_oid.begin() + num_childs[oid + 1],
					oid);
				std::iota(symbolic_iteration.child_id.begin() + num_childs[oid],
					symbolic_iteration.child_id.begin() + num_childs[oid + 1],
					0);
			}

			#pragma omp single
			mid_step_function(2);

			/* !!!!!!!!!!!!!!!!
			step (3)
			 !!!!!!!!!!!!!!!! */

			#pragma omp for schedule(static)
			for (size_t oid = 0; oid < symbolic_iteration.num_object; ++oid) {
				auto id = symbolic_iteration.parent_oid[oid];

				/* generate graph */
				symbolic_iteration.real[oid] = real[id];
				symbolic_iteration.imag[oid] = imag[id];
				char* end = rule->populate_child(objects.begin() + object_begin[id],
					objects.begin() + object_begin[id + 1],
					symbolic_iteration.child_id[oid],
					symbolic_iteration.real[oid], symbolic_iteration.imag[oid], symbolic_iteration.placeholder[thread_id]);

				symbolic_iteration.size[oid] = std::distance(symbolic_iteration.placeholder[thread_id], end);

				/* compute hash */
				symbolic_iteration.hash[oid] = rule->hasher(symbolic_iteration.placeholder[thread_id], end);
			}
		}

		mid_step_function(3);
	}

	/*
	compute interferences
	*/
	void symbolic_iteration::compute_collisions() {
		if (num_object == 0)
			return;

		bool fast = false;
		bool skip_test = num_object < utils::min_vector_size;
		size_t test_size = skip_test ? 0 : num_object*collision_test_proportion;

		/*
		function for partition
		*/
		auto static partitioner = [&](size_t const &oid) {
			/* check if graph is unique */
			if (!is_unique[oid])
				return false;

			/* check for zero probability */
			PROBA_TYPE r = real[oid];
			PROBA_TYPE i = imag[oid];

			return r*r + i*i > tolerance;
		};

		/* !!!!!!!!!!!!!!!!
		step (4)
		 !!!!!!!!!!!!!!!! */

		if (!skip_test) {
			#pragma omp parallel for schedule(static)
			for (size_t oid = 0; oid < test_size; ++oid) { //size_t oid = oid[i];
				/* accessing key */
				tbb::concurrent_hash_map<size_t, size_t>::accessor it;
				if (elimination_map.insert(it, {hash[oid], oid})) {
					is_unique[oid] = true; /* keep this graph */
				} else {
					/* if it exist add the probabilities */
					real[it->second] += real[oid];
					imag[it->second] += imag[oid];

					/* discard this graph */
					is_unique[oid] = false;
				}
			}

			fast = test_size - elimination_map.size() < test_size*collision_test_proportion;

			/* check if we should continue */
			if (fast) {
				/* get all unique graphs with a non zero probability */
				auto partitioned_it = __gnu_parallel::partition(next_oid.begin(), next_oid.begin() + test_size, partitioner);
				partitioned_it = std::rotate(partitioned_it, next_oid.begin() + test_size, next_oid.begin() + num_object);
				num_object_after_interferences = std::distance(next_oid.begin(), partitioned_it);
			}
		}

		if (!fast) {
			#pragma omp parallel for schedule(static)
			for (size_t oid = test_size; oid < num_object; ++oid) { //size_t oid = oid[i];
				/* accessing key */
				tbb::concurrent_hash_map<size_t, size_t>::accessor it;
				if (elimination_map.insert(it, {hash[oid], oid})) {
					is_unique[oid] = true; /* keep this graph */
				} else {
					/* if it exist add the probabilities */
					real[it->second] += real[oid];
					imag[it->second] += imag[oid];

					/* discard this graph */
					is_unique[oid] = false;
				}
			}

			/* get all unique graphs with a non zero probability */
			auto partitioned_it = __gnu_parallel::partition(next_oid.begin(), next_oid.begin() + num_object, partitioner);
			num_object_after_interferences = std::distance(next_oid.begin(), partitioned_it);
		}
				
		elimination_map.clear();
	}

	/*
	finalize iteration
	*/
	void symbolic_iteration::finalize(rule_t const *rule, it_t const &last_iteration, it_t &next_iteration, debug_t mid_step_function=[](int){}) {
		if (num_object == 0) {
			next_iteration.num_object = 0;
			return;
		}
		
		mid_step_function(4);

		/* !!!!!!!!!!!!!!!!
		step (5)
		 !!!!!!!!!!!!!!!! */

		long long int max_num_object = get_max_num_object(next_iteration, last_iteration, *this) / 2;
		max_num_object = std::max(max_num_object, (long long int)utils::min_vector_size);

		if (num_object_after_interferences > max_num_object) {

			/* generate random selectors */
			#pragma omp parallel for schedule(static)
			for (size_t i = 0; i < num_object_after_interferences; ++i)  {
				size_t oid = next_oid[i];

				PROBA_TYPE r = real[oid];
				PROBA_TYPE i = imag[oid];

				double random_number = utils::unfiorm_from_hash(hash[oid]); //random_generator();
				random_selector[oid] = std::log( -std::log(1 - random_number) / (r*r + i*i));
			}

			/* select graphs according to random selectors */
			__gnu_parallel::nth_element(next_oid.begin(), next_oid.begin() + max_num_object, next_oid.begin() + num_object_after_interferences,
			[&](size_t const &oid1, size_t const &oid2) {
				return random_selector[oid1] < random_selector[oid2];
			});

			next_iteration.num_object = max_num_object;
		} else
			next_iteration.num_object = num_object_after_interferences;

		mid_step_function(5);

		/* !!!!!!!!!!!!!!!!
		step (6)
		 !!!!!!!!!!!!!!!! */

		/* sort to make memory access more continuous */
		__gnu_parallel::sort(next_oid.begin(), next_oid.begin() + next_iteration.num_object);

		/* resize new step variables */
		next_iteration.resize(next_iteration.num_object);
				
		/* prepare for partial sum */
		#pragma omp parallel for schedule(static)
		for (size_t oid = 0; oid < next_iteration.num_object; ++oid) {
			size_t id = next_oid[oid];

			next_iteration.object_begin[oid + 1] = size[id];

			/* assign magnitude */
			next_iteration.real[oid] = real[id];
			next_iteration.imag[oid] = imag[id];
		}

		__gnu_parallel::partial_sum(next_iteration.object_begin.begin() + 1,
			next_iteration.object_begin.begin() + next_iteration.num_object + 1,
			next_iteration.object_begin.begin() + 1);

		next_iteration.allocate(next_iteration.object_begin[next_iteration.num_object]);

		mid_step_function(6);

		/* !!!!!!!!!!!!!!!!
		step (7)
		 !!!!!!!!!!!!!!!! */

		#pragma omp parallel
		{
			auto thread_id = omp_get_thread_num();
			PROBA_TYPE real_, imag_;

			#pragma omp for schedule(static)
			for (size_t oid = 0; oid < next_iteration.num_object; ++oid) {
				auto id = next_oid[oid];
				auto this_parent_oid = parent_oid[id];
				
				rule->populate_child(last_iteration.objects.begin() + last_iteration.object_begin[this_parent_oid],
					last_iteration.objects.begin() + last_iteration.object_begin[this_parent_oid + 1],
					child_id[id],
					real_, imag_,
					next_iteration.objects.begin() + next_iteration.object_begin[oid]);
			}
		}
		
		mid_step_function(7);
	}

	/*
	normalize
	*/
	void iteration::normalize() {
		/* !!!!!!!!!!!!!!!!
		step (8)
		 !!!!!!!!!!!!!!!! */

		total_proba = 0;

		#pragma omp parallel for reduction(+:total_proba)
		for (size_t oid = 0; oid < num_object; ++oid) {
			PROBA_TYPE r = real[oid];
			PROBA_TYPE i = imag[oid];

			total_proba += r*r + i*i;
		}

		PROBA_TYPE normalization_factor = std::sqrt(total_proba);

		#pragma omp parallel for
		for (size_t oid = 0; oid < num_object; ++oid) {
			real[oid] /= normalization_factor;
			imag[oid] /= normalization_factor;
		}
	}
}