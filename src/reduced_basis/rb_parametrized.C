// rbOOmit: An implementation of the Certified Reduced Basis method.
// Copyright (C) 2009, 2010 David J. Knezevic

// This file is part of rbOOmit.

// rbOOmit is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.

// rbOOmit is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

// libMesh includes
#include "libmesh/int_range.h"
#include "libmesh/simple_range.h"
#include "libmesh/xdr_cxx.h"

// rbOOmit includes
#include "libmesh/rb_parametrized.h"

// C++ includes
#include <sstream>
#include <fstream>
#include <algorithm> // std::min_element

namespace libMesh
{

RBParametrized::RBParametrized()
  :
  verbose_mode(false),
  parameters_initialized(false)
{
}

RBParametrized::~RBParametrized() = default;

void RBParametrized::clear()
{
  parameters.clear();
  parameters_min.clear();
  parameters_max.clear();
  parameters_initialized = false;
}

void RBParametrized::initialize_parameters(const RBParameters & mu_min_in,
                                           const RBParameters & mu_max_in,
                                           const std::map<std::string, std::vector<Real>> & discrete_parameter_values)
{
  // Check that the min/max vectors have the same size.
  libmesh_error_msg_if(mu_min_in.n_parameters() != mu_max_in.n_parameters(),
                       "Error: Invalid mu_min/mu_max in initialize_parameters(), different number of parameters.");
  libmesh_error_msg_if(mu_min_in.n_samples() != 1 ||
                       mu_max_in.n_samples() != 1,
                       "Error: Invalid mu_min/mu_max in initialize_parameters(), only 1 sample supported.");

  // Ensure all the values are valid for min and max.
  auto pr_min = mu_min_in.begin_serialized();
  auto pr_max = mu_max_in.begin_serialized();
  for (; pr_min != mu_min_in.end_serialized(); ++pr_min, ++pr_max)
    libmesh_error_msg_if((*pr_min).second > (*pr_max).second,
                         "Error: Invalid mu_min/mu_max in RBParameters constructor.");

  parameters_min = mu_min_in;
  parameters_max = mu_max_in;

  // Add in min/max values due to the discrete parameters
  for (const auto & [name, vals] : discrete_parameter_values)
    {
      libmesh_error_msg_if(vals.empty(), "Error: List of discrete parameters for " << name << " is empty.");

      Real min_val = *std::min_element(vals.begin(), vals.end());
      Real max_val = *std::max_element(vals.begin(), vals.end());

      libmesh_assert_less_equal(min_val, max_val);

      parameters_min.set_value(name, min_val);
      parameters_max.set_value(name, max_val);
    }

    _discrete_parameter_values = discrete_parameter_values;

  parameters_initialized = true;

  // Initialize the current parameters to parameters_min
  set_parameters(parameters_min);
}

void RBParametrized::initialize_parameters(const RBParametrized & rb_parametrized)
{
  initialize_parameters(rb_parametrized.get_parameters_min(),
                        rb_parametrized.get_parameters_max(),
                        rb_parametrized.get_discrete_parameter_values());
}

unsigned int RBParametrized::get_n_params() const
{
  libmesh_error_msg_if(!parameters_initialized, "Error: parameters not initialized in RBParametrized::get_n_params");

  libmesh_assert_equal_to ( parameters_min.n_parameters(), parameters_max.n_parameters() );

  return parameters_min.n_parameters();
}

unsigned int RBParametrized::get_n_continuous_params() const
{
  libmesh_error_msg_if(!parameters_initialized, "Error: parameters not initialized in RBParametrized::get_n_continuous_params");

  libmesh_assert(get_n_params() >= get_n_discrete_params());

  return static_cast<unsigned int>(get_n_params() - get_n_discrete_params());
}

unsigned int RBParametrized::get_n_discrete_params() const
{
  libmesh_error_msg_if(!parameters_initialized, "Error: parameters not initialized in RBParametrized::get_n_discrete_params");

  return cast_int<unsigned int>
    (get_discrete_parameter_values().size());
}

#ifdef LIBMESH_ENABLE_DEPRECATED
std::set<std::string> RBParametrized::get_parameter_names() const
{
  libmesh_deprecated();
  libmesh_error_msg_if(!parameters_initialized, "Error: parameters not initialized in RBParametrized::get_parameter_names");

  std::set<std::string> parameter_names;
  for (const auto & pr : parameters_min)
    parameter_names.insert(pr.first);

  return parameter_names;
}
#endif // LIBMESH_ENABLE_DEPRECATED

bool RBParametrized::set_parameters(const RBParameters & params)
{
  libmesh_error_msg_if(!parameters_initialized, "Error: parameters not initialized in RBParametrized::set_parameters");

  // Terminate if params has the wrong number of parameters or samples.
  // If the parameters are outside the min/max range, return false.
  const bool valid_params = check_if_valid_params(params);

  // Make a copy of params (default assignment operator just does memberwise copy, which is sufficient here)
  this->parameters = params;

  return valid_params;
}

const RBParameters & RBParametrized::get_parameters() const
{
  libmesh_error_msg_if(!parameters_initialized, "Error: parameters not initialized in RBParametrized::get_parameters");

  return parameters;
}

const RBParameters & RBParametrized::get_parameters_min() const
{
  libmesh_error_msg_if(!parameters_initialized, "Error: parameters not initialized in RBParametrized::get_parameters_min");

  return parameters_min;
}

const RBParameters & RBParametrized::get_parameters_max() const
{
  libmesh_error_msg_if(!parameters_initialized, "Error: parameters not initialized in RBParametrized::get_parameters_max");

  return parameters_max;
}

Real RBParametrized::get_parameter_min(const std::string & param_name) const
{
  libmesh_error_msg_if(!parameters_initialized, "Error: parameters not initialized in RBParametrized::get_parameter_min");

  return parameters_min.get_value(param_name);
}

Real RBParametrized::get_parameter_max(const std::string & param_name) const
{
  libmesh_error_msg_if(!parameters_initialized, "Error: parameters not initialized in RBParametrized::get_parameter_max");

  return parameters_max.get_value(param_name);
}

void RBParametrized::print_parameters() const
{
  libmesh_error_msg_if(!parameters_initialized, "Error: parameters not initialized in RBParametrized::print_current_parameters");

  get_parameters().print();
}

void RBParametrized::write_parameter_data_to_files(const std::string & continuous_param_file_name,
                                                   const std::string & discrete_param_file_name,
                                                   const bool write_binary_data)
{
  write_parameter_ranges_to_file(continuous_param_file_name, write_binary_data);
  write_discrete_parameter_values_to_file(discrete_param_file_name, write_binary_data);
}

void RBParametrized::write_parameter_ranges_to_file(const std::string & file_name,
                                                    const bool write_binary_data)
{
  // The writing mode: ENCODE for binary, WRITE for ASCII
  XdrMODE mode = write_binary_data ? ENCODE : WRITE;

  // Write out the parameter ranges
  Xdr parameter_ranges_out(file_name, mode);
  unsigned int n_continuous_params = get_n_continuous_params();
  parameter_ranges_out << n_continuous_params;

  // Note: the following loops are not candidates for structured
  // bindings syntax because the Xdr APIs which they call are not
  // defined for const references. We must therefore make copies to
  // call these functions.
  for (const auto & pr : get_parameters_min())
    {
      std::string param_name = pr.first;
      if (!is_discrete_parameter(param_name))
        {
          Real param_value = get_parameters_min().get_value(param_name);
          parameter_ranges_out << param_name << param_value;
        }
    }
  for (const auto & pr : get_parameters_max())
    {
      std::string param_name = pr.first;
      if (!is_discrete_parameter(param_name))
        {
          Real param_value = get_parameters_max().get_value(param_name);
          parameter_ranges_out << param_name << param_value;
        }
    }

  parameter_ranges_out.close();
}

void RBParametrized::write_discrete_parameter_values_to_file(const std::string & file_name,
                                                             const bool write_binary_data)
{
  // write out the discrete parameters, if we have any
  if (get_n_discrete_params() > 0)
    {
      // The writing mode: ENCODE for binary, WRITE for ASCII
      XdrMODE mode = write_binary_data ? ENCODE : WRITE;

      Xdr discrete_parameters_out(file_name, mode);
      unsigned int n_discrete_params = get_n_discrete_params();
      discrete_parameters_out << n_discrete_params;

      // Note: the following loops are not candidates for structured
      // bindings syntax because the Xdr APIs which they call are not
      // defined for const references. We must therefore make copies
      // to call these functions.
      for (const auto & pr : get_discrete_parameter_values())
        {
          std::string param_name = pr.first;
          auto n_discrete_values = cast_int<unsigned int>(pr.second.size());
          discrete_parameters_out << param_name << n_discrete_values;

          for (unsigned int i=0; i<n_discrete_values; i++)
            {
              Real discrete_value = pr.second[i];
              discrete_parameters_out << discrete_value;
            }
        }
    }
}

void RBParametrized::read_parameter_data_from_files(const std::string & continuous_param_file_name,
                                                    const std::string & discrete_param_file_name,
                                                    const bool read_binary_data)
{
  RBParameters param_min;
  RBParameters param_max;
  read_parameter_ranges_from_file(continuous_param_file_name,
                                  read_binary_data,
                                  param_min,
                                  param_max);

  std::map<std::string, std::vector<Real>> discrete_parameter_values_in;
  read_discrete_parameter_values_from_file(discrete_param_file_name,
                                           read_binary_data,
                                           discrete_parameter_values_in);

  initialize_parameters(param_min, param_max, discrete_parameter_values_in);
}

void RBParametrized::read_parameter_ranges_from_file(const std::string & file_name,
                                                     const bool read_binary_data,
                                                     RBParameters & param_min,
                                                     RBParameters & param_max)
{
  // The reading mode: DECODE for binary, READ for ASCII
  XdrMODE mode = read_binary_data ? DECODE : READ;

  // Read in the parameter ranges
  Xdr parameter_ranges_in(file_name, mode);
  unsigned int n_continuous_params;
  parameter_ranges_in >> n_continuous_params;

  for (unsigned int i=0; i<n_continuous_params; i++)
    {
      std::string param_name;
      Real param_value;

      parameter_ranges_in >> param_name;
      parameter_ranges_in >> param_value;

      param_min.set_value(param_name, param_value);
    }
  for (unsigned int i=0; i<n_continuous_params; i++)
    {
      std::string param_name;
      Real param_value;

      parameter_ranges_in >> param_name;
      parameter_ranges_in >> param_value;

      param_max.set_value(param_name, param_value);
    }

  parameter_ranges_in.close();
}

void RBParametrized::read_discrete_parameter_values_from_file(const std::string & file_name,
                                                              const bool read_binary_data,
                                                              std::map<std::string, std::vector<Real>> & discrete_parameter_values)
{
  // read in the discrete parameters, if we have any
  std::ifstream check_if_file_exists(file_name.c_str());
  if (check_if_file_exists.good())
    {
      // The reading mode: DECODE for binary, READ for ASCII
      XdrMODE mode = read_binary_data ? DECODE : READ;

      // Read in the parameter ranges
      Xdr discrete_parameter_values_in(file_name, mode);
      unsigned int n_discrete_params;
      discrete_parameter_values_in >> n_discrete_params;

      for (unsigned int i=0; i<n_discrete_params; i++)
        {
          std::string param_name;
          discrete_parameter_values_in >> param_name;

          unsigned int n_discrete_values;
          discrete_parameter_values_in >> n_discrete_values;

          std::vector<Real> discrete_values(n_discrete_values);
          for (auto & val : discrete_values)
            discrete_parameter_values_in >> val;

          discrete_parameter_values[param_name] = discrete_values;
        }
    }
}

bool RBParametrized::is_discrete_parameter(const std::string & mu_name) const
{
  libmesh_error_msg_if(!parameters_initialized,
                       "Error: parameters not initialized in RBParametrized::is_discrete_parameter");

  return _discrete_parameter_values.count(mu_name);
}

const std::map<std::string, std::vector<Real>> & RBParametrized::get_discrete_parameter_values() const
{
  libmesh_error_msg_if(!parameters_initialized, "Error: parameters not initialized in RBParametrized::get_discrete_parameter_values");

  return _discrete_parameter_values;
}

void RBParametrized::print_discrete_parameter_values() const
{
  for (const auto & [name, values] : get_discrete_parameter_values())
    {
      libMesh::out << "Discrete parameter " << name << ", values: ";

      for (const auto & value : values)
        libMesh::out << value << " ";
      libMesh::out << std::endl;
    }
}

bool RBParametrized::check_if_valid_params(const RBParameters &params) const
{
  // Check if number of parameters are correct.
  libmesh_error_msg_if(params.n_parameters() != get_n_params(),
                       "Error: Number of parameters don't match; found "
                       << params.n_parameters() << ", expected "
                       << get_n_params());

  bool is_valid = true;
  std::string prev_param_name = "";
  for (const auto & [param_name, sample_vec] : params)
    {
      std::size_t sample_idx = 0;
      const Real & min_value = get_parameter_min(param_name);
      const Real & max_value = get_parameter_max(param_name);
      for (const auto & value_vec : sample_vec)
        {
          for (const auto & value : value_vec)
            {
              // Check every parameter value (including across samples and vector-values)
              // to ensure it's within the min/max range.
              const bool outside_range = ((value < min_value) || (value > max_value));
              is_valid = is_valid && !outside_range;
              if (outside_range && verbose_mode)
                {
                  libMesh::out << "Warning: parameter " << param_name << " value="
                               << value << " outside acceptable range: ("
                               << min_value << ", " << max_value << ")";
                }

              // For discrete params, make sure params.get_value(param_name) is sufficiently
              // close to one of the discrete parameter values.
              // Note that vector-values not yet supported in discrete parameters,
              // and the .get_sample_value() call will throw an error if the user
              // tries to do it.
              if (const auto it = get_discrete_parameter_values().find(param_name);
                  it != get_discrete_parameter_values().end())
                {
                  const bool is_value_discrete =
                    is_value_in_list(params.get_sample_value(param_name, sample_idx),
                                     it->second,
                                     TOLERANCE);
                  is_valid = is_valid && is_value_discrete;
                  if (!is_value_discrete && verbose_mode)
                    libMesh::out << "Warning: parameter " << param_name << " value="
                                 << value << " is not in discrete value list.";
                }
            }
          ++sample_idx;
        }
    }
  return is_valid;
}

Real RBParametrized::get_closest_value(Real value, const std::vector<Real> & list_of_values)
{
  libmesh_error_msg_if(list_of_values.empty(), "Error: list_of_values is empty.");

  Real min_distance = std::numeric_limits<Real>::max();
  Real closest_val = 0.;
  for (const auto & current_value : list_of_values)
    {
      Real distance = std::abs(value - current_value);
      if (distance < min_distance)
        {
          min_distance = distance;
          closest_val = current_value;
        }
    }

  return closest_val;
}

bool RBParametrized::is_value_in_list(Real value, const std::vector<Real> & list_of_values, Real tol)
{
  Real closest_value = get_closest_value(value, list_of_values);

  // Check if relative tolerance is satisfied
  Real rel_error = std::abs(value - closest_value) / std::abs(value);
  if (rel_error <= tol)
    {
      return true;
    }

  // If relative tolerance isn't satisfied, we should still check an absolute
  // error, since relative tolerance can be misleading if value is close to zero
  Real abs_error = std::abs(value - closest_value);
  return (abs_error <= tol);
}

} // namespace libMesh
