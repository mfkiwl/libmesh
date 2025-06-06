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



#include "libmesh/libmesh_config.h"

#ifdef LIBMESH_TRILINOS_HAVE_DTK

#include "libmesh/dtk_solution_transfer.h"
#include "libmesh/system.h"

#include "libmesh/ignore_warnings.h"

// Trilinos Includes
#include <Teuchos_RCP.hpp>
#include <Teuchos_GlobalMPISession.hpp>
#include <Teuchos_Ptr.hpp>
#include <Teuchos_DefaultMpiComm.hpp>
#include <Teuchos_OpaqueWrapper.hpp>

// DTK Includes
#include <DTK_MeshManager.hpp>
#include <DTK_MeshContainer.hpp>
#include <DTK_FieldManager.hpp>
#include <DTK_FieldContainer.hpp>
#include <DTK_FieldTools.hpp>
#include <DTK_CommTools.hpp>
#include <DTK_CommIndexer.hpp>

#include "libmesh/restore_warnings.h"

namespace libMesh
{

DTKSolutionTransfer::DTKSolutionTransfer(const libMesh::Parallel::Communicator & comm) :
  SolutionTransfer(comm)
{
  //comm_default = Teuchos::DefaultComm<int>::getComm();
  comm_default = Teuchos::rcp(new Teuchos::MpiComm<int>(Teuchos::rcp(new Teuchos::OpaqueWrapper<MPI_Comm>(comm.get()))));
}

DTKSolutionTransfer::~DTKSolutionTransfer() = default;

void
DTKSolutionTransfer::transfer(const Variable & from_var,
                              const Variable & to_var)
{
  libmesh_experimental();

  EquationSystems * from_es = &from_var.system()->get_equation_systems();
  EquationSystems * to_es = &to_var.system()->get_equation_systems();

  // Possibly make an Adapter for from_es
  if (!adapters.count(from_es))
    adapters[from_es] = std::make_unique<DTKAdapter>(comm_default, *from_es);

  // Possibly make an Adapter for to_es
  if (!adapters.count(to_es))
    adapters[to_es] = std::make_unique<DTKAdapter>(comm_default, *to_es);

  DTKAdapter * from_adapter = adapters[from_es].get();
  DTKAdapter * to_adapter = adapters[to_es].get();

  // We first try emplacing a nullptr into the "dtk_maps" map with the desired key
  auto [it, emplaced] = dtk_maps.emplace(std::make_pair(from_es, to_es), nullptr);

  // If the emplace succeeded, that means it was a new entry in the
  // map, so we need to actually construct the object.
  if (emplaced)
    {
      libmesh_assert(from_es->get_mesh().mesh_dimension() == to_es->get_mesh().mesh_dimension());

      it->second = std::make_unique<shared_domain_map_type>(comm_default, from_es->get_mesh().mesh_dimension(), true);

      // The tolerance here is for the "contains_point()" implementation in DTK.  Set a larger value for a looser tolerance...
      it->second->setup(from_adapter->get_mesh_manager(), to_adapter->get_target_coords(), 30*Teuchos::ScalarTraits<double>::eps());
    }

  DTKAdapter::RCP_Evaluator from_evaluator = from_adapter->get_variable_evaluator(from_var.name());
  Teuchos::RCP<DataTransferKit::FieldManager<DTKAdapter::FieldContainerType>> to_values = to_adapter->get_values_to_fill(to_var.name());

  it->second->apply(from_evaluator, to_values);

  if (it->second->getMissedTargetPoints().size())
    libMesh::out << "Warning: Some points were missed in the transfer of " << from_var.name() << " to " << to_var.name() << "!" << std::endl;

  to_adapter->update_variable_values(to_var.name());
}

} // namespace libMesh

#endif // #ifdef LIBMESH_TRILINOS_HAVE_DTK
