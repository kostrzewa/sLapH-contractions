/*! @file global_data_utils.h
 *  Functions for input handling
 *
 *  @author Bastian Knippschild
 *  @author Markus Werner
 */

#pragma once

#include <array>
#include <iostream>
#include <string>

#include "global_data_typedefs.h"
#include "typedefs.h"

namespace global_data_utils {

/*! Makes a quark object from a string */
quark make_quark(const std::string &quark_string);
/*! Enforces that @em quarks is filled with valid values and terminates the
 *  program with an errormessage otherwise
 */
void quark_check(quark quarks);

/*! Makes an operator list object from a string */
Operators make_operator_list(const std::string &operator_string);

/*! Makes an Correlators object from a string */
Correlators_2 make_correlator(const std::string &correlator_string);

}  // end of namespace global_data_utils
