#include "../src/lib/iqs.hpp"
#include "../src/rules/quantum_computer.hpp"

#include <iostream>

void print_it(iqs::it_t const &iter) {
	for (auto gid = 0; gid < iter.num_object; ++gid) {
		auto begin = iter.objects.begin() + iter.object_begin[gid];
		auto end = iter.objects.begin() + iter.object_begin[gid + 1];

		std::cout << "\t" << iter.real[gid] << (iter.imag[gid] < 0 ? " - " : " + ") << std::abs(iter.imag[gid]) << "i  ";
		for (auto it = begin; it != end; ++it)
			std::cout << (*it ? '1' : '0');
		std::cout << "\n";
	}
}

int main(int argc, char* argv[]) {
	iqs::rule_t *H1 = new iqs::rules::quantum_computer::hadamard(1);
	iqs::rule_t *H2 = new iqs::rules::quantum_computer::hadamard(2);
	iqs::rule_t *CNOT = new iqs::rules::quantum_computer::cnot(1, 3);
	iqs::rule_t *X2 = new iqs::rules::quantum_computer::Xgate(2);
	iqs::rule_t *Y0 = new iqs::rules::quantum_computer::Ygate(0);
	iqs::rule_t *Z3 = new iqs::rules::quantum_computer::Zgate(3);
	iqs::sy_it_t sy_it; iqs::it_t buffer;

	/* constructing a starting state with different size state */
	iqs::it_t state;
	char starting_state_1[] = {true, true, false, false};
	char starting_state_2[] = {false, true, true, false, true};
	state.append(starting_state_1, starting_state_1 + 4, 1/std::sqrt(2), 0);
	state.append(starting_state_2, starting_state_2 + 5, 0, 1/std::sqrt(2));
	std::cout << "initial_state:\n"; print_it(state);

	iqs::simulate(state, H1, buffer, sy_it);
	std::cout << "\nhadamard on second qubit:\n"; print_it(state);

	iqs::simulate(state, H2, buffer, sy_it);
	std::cout << "\nhadamard on third qubit:\n"; print_it(state);

	iqs::simulate(state, CNOT, buffer, sy_it);
	std::cout << "\ncnot on fourth qubit controled by second qubit:\n"; print_it(state);

	iqs::simulate(state, X2, buffer, sy_it);
	std::cout << "\nX on third qubit:\n"; print_it(state);

	iqs::simulate(state, Y0, buffer, sy_it);
	std::cout << "\nY on first qubit:\n"; print_it(state);

	iqs::simulate(state, Z3, buffer, sy_it);
	std::cout << "\nZ on fourth qubit:\n"; print_it(state);

	iqs::simulate(state, Z3, buffer, sy_it);
	iqs::simulate(state, Y0, buffer, sy_it);
	iqs::simulate(state, X2, buffer, sy_it);
	iqs::simulate(state, CNOT, buffer, sy_it);
	iqs::simulate(state, H2, buffer, sy_it);
	iqs::simulate(state, H1, buffer, sy_it);
	std::cout << "\napplied all previous gates in reverse order:\n";  print_it(state);
}