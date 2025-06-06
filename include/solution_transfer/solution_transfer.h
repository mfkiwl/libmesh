// The libMesh Finite Element Library.
// Copyright (C) 2002-2025 Benjamin S. Kirk, John W. Peterson, Roy H. Stogner

// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA



#ifndef SOLUTIONTRANSFER_H
#define SOLUTIONTRANSFER_H

#include "libmesh/libmesh_common.h"
#include "libmesh/equation_systems.h"
#include "libmesh/parallel_object.h"

#include <string>
#include <map>

namespace libMesh {

/**
 * Base class for objects that allow transferring variable values
 * between different systems with different meshes.
 *
 * \author Derek Gaston
 * \date 2013
 * \brief Base class for all SolutionTransfer implementations.
 */
class SolutionTransfer : public ParallelObject
{
public:

  SolutionTransfer(const libMesh::Parallel::Communicator & comm_in) :
    ParallelObject(comm_in)
  {}

  virtual ~SolutionTransfer() = default;

  /**
   * Transfer the values of a variable to another.
   *
   * This is meant for transferring values from one EquationSystems to another
   * even in the case of having different meshes.
   */
  virtual void transfer(const Variable & from_var, const Variable & to_var) = 0;
};

} // namespace libMesh

#endif // #define SOLUTIONTRANSFER_H
