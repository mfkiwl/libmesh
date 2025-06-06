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

// C++ includes
#include <algorithm> // for std::fill
#include <cstdlib> // *must* precede <cmath> for proper std:abs() on PGI, Sun Studio CC
#include <cmath>    // for sqrt
#include <set>
#include <sstream> // for ostringstream
#include <unordered_map>
#include <unordered_set>

// Local Includes
#include "libmesh/dof_map.h"
#include "libmesh/elem.h"
#include "libmesh/equation_systems.h"
#include "libmesh/error_vector.h"
#include "libmesh/fe.h"
#include "libmesh/fe_interface.h"
#include "libmesh/libmesh_common.h"
#include "libmesh/libmesh_logging.h"
#include "libmesh/mesh_base.h"
#include "libmesh/mesh_refinement.h"
#include "libmesh/numeric_vector.h"
#include "libmesh/quadrature.h"
#include "libmesh/system.h"
#include "libmesh/diff_physics.h"
#include "libmesh/fem_system.h"
#include "libmesh/implicit_system.h"
#include "libmesh/partitioner.h"
#include "libmesh/adjoint_refinement_estimator.h"
#include "libmesh/enum_error_estimator_type.h"
#include "libmesh/enum_norm_type.h"
#include "libmesh/utility.h"

#ifdef LIBMESH_ENABLE_AMR

namespace libMesh
{

//-----------------------------------------------------------------
// AdjointRefinementErrorEstimator

// As of 10/2/2012, this function implements a 'brute-force' adjoint based QoI
// error estimator, using the following relationship:
// Q(u) - Q(u_h) \approx - R( (u_h)_(h/2), z_(h/2) ) .
// where: Q(u) is the true QoI
// u_h is the approximate primal solution on the current FE space
// Q(u_h) is the approximate QoI
// z_(h/2) is the adjoint corresponding to Q, on a richer FE space
// (u_h)_(h/2) is a projection of the primal solution on the richer FE space
// By richer FE space, we mean a grid that has been refined once and a polynomial order
// that has been increased once, i.e. one h and one p refinement

// Both a global QoI error estimate and element wise error indicators are included
// Note that the element wise error indicators slightly over estimate the error in
// each element

AdjointRefinementEstimator::AdjointRefinementEstimator() :
  ErrorEstimator(),
  number_h_refinements(1),
  number_p_refinements(0),
  _residual_evaluation_physics(nullptr),
  _adjoint_evaluation_physics(nullptr),
  _qoi_set(QoISet())
{
  // We're not actually going to use error_norm; our norms are
  // absolute values of QoI error.
  error_norm = INVALID_NORM;
}

ErrorEstimatorType AdjointRefinementEstimator::type() const
{
  return ADJOINT_REFINEMENT;
}

void AdjointRefinementEstimator::estimate_error (const System & _system,
                                                 ErrorVector & error_per_cell,
                                                 const NumericVector<Number> * solution_vector,
                                                 bool /*estimate_parent_error*/)
{
  // We have to break the rules here, because we can't refine a const System
  System & mutable_system = const_cast<System &>(_system);

  // We really have to break the rules, because we can't do an
  // adjoint_solve without a matrix.
  ImplicitSystem & system = dynamic_cast<ImplicitSystem &>(mutable_system);

  // An EquationSystems reference will be convenient.
  EquationSystems & es = system.get_equation_systems();

  // The current mesh
  MeshBase & mesh = es.get_mesh();

  // Get coarse grid adjoint solutions.  This should be a relatively
  // quick (especially with preconditioner reuse) way to get a good
  // initial guess for the fine grid adjoint solutions.  More
  // importantly, subtracting off a coarse adjoint approximation gives
  // us better local error indication, and subtracting off *some* lift
  // function is necessary for correctness if we have heterogeneous
  // adjoint Dirichlet conditions.

  // Solve the adjoint problem(s) on the coarse FE space
  // Only if the user didn't already solve it for us
  // If _adjoint_evaluation_physics pointer is not null, swap
  // the current physics with the one held by _adjoint_evaluation_physics
  // before assembling the adjoint problem
  if (!system.is_adjoint_already_solved())
  {
    // Swap in different physics if needed
    if (_adjoint_evaluation_physics)
      dynamic_cast<DifferentiableSystem &>(system).push_physics(*_adjoint_evaluation_physics);

    // Solve the adjoint problem, remember physics swap also resets the cache, so
    // we will assemble again, otherwise we just take the transpose
    system.adjoint_solve(_qoi_set);

    if (_adjoint_evaluation_physics)
      dynamic_cast<DifferentiableSystem &>(system).pop_physics();
  }

  // Loop over all the adjoint problems and, if any have heterogenous
  // Dirichlet conditions, get the corresponding coarse lift
  // function(s)
  for (unsigned int j=0,
       n_qois = system.n_qois(); j != n_qois; j++)
    {
      // Skip this QoI if it is not in the QoI Set or if there are no
      // heterogeneous Dirichlet boundaries for it
      if (_qoi_set.has_index(j) &&
          system.get_dof_map().has_adjoint_dirichlet_boundaries(j))
        {
          // Next, we are going to build up the residual for evaluating the flux QoI
          NumericVector<Number> * coarse_residual = nullptr;

          // The definition of a flux QoI is R(u^h, L) where R is the residual as defined
          // by a conservation law. Therefore, if we are using stabilization, the
          // R should be specified by the user via the residual_evaluation_physics

          // If the residual physics pointer is not null, use it when
          // evaluating here.
          {
            if (_residual_evaluation_physics)
              dynamic_cast<DifferentiableSystem &>(system).push_physics(*_residual_evaluation_physics);

            // Assemble without applying constraints, to capture the solution values on the boundary
            system.assembly(true, false, false, true);

            // Get the residual vector (no constraints applied on boundary, so we wont blow away the lift)
            coarse_residual = &system.get_vector("RHS Vector");
            coarse_residual->close();

            // Now build the lift function and add it to the system vectors
            std::ostringstream liftfunc_name;
            liftfunc_name << "adjoint_lift_function" << j;

            system.add_vector(liftfunc_name.str());

            // Initialize lift with coarse adjoint solve associate with this flux QoI to begin with
            system.get_vector(liftfunc_name.str()).init(system.get_adjoint_solution(j), false);

            // Build the actual lift using adjoint dirichlet conditions
            system.get_dof_map().enforce_adjoint_constraints_exactly
              (system.get_vector(liftfunc_name.str()), static_cast<unsigned int>(j));

            // Compute the flux R(u^h, L)
            std::cout<<"The flux QoI "<<static_cast<unsigned int>(j)<<" is: "<<coarse_residual->dot(system.get_vector(liftfunc_name.str()))<<std::endl<<std::endl;

            // Restore the original physics if needed
            if (_residual_evaluation_physics)
              dynamic_cast<DifferentiableSystem &>(system).pop_physics();
          }
        } // End if QoI in set and flux/dirichlet boundary QoI
    } // End loop over QoIs

  // We'll want to back up all coarse grid vectors
  std::map<std::string, std::unique_ptr<NumericVector<Number>>> coarse_vectors;
  for (const auto & [var_name, vec] : as_range(system.vectors_begin(), system.vectors_end()))
    coarse_vectors[var_name] = vec->clone();

  // Back up the coarse solution and coarse local solution
  std::unique_ptr<NumericVector<Number>> coarse_solution = system.solution->clone();
  std::unique_ptr<NumericVector<Number>> coarse_local_solution = system.current_local_solution->clone();

  // We need to make sure that the coarse adjoint vectors used in the
  // calculations below are preserved during reinit, regardless of how
  // the user is treating them in their code
  // The adjoint lift function we have defined above is set to be preserved
  // by default
  std::vector<bool> old_adjoints_projection_settings(system.n_qois());
  for (auto j : make_range(system.n_qois()))
    {
      if (_qoi_set.has_index(j))
        {
          // Get the vector preservation setting for this adjoint vector
          auto adjoint_vector_name = system.vector_name(system.get_adjoint_solution(j));
          auto old_adjoint_vector_projection_setting = system.vector_preservation(adjoint_vector_name);

          // Save for restoration later on
          old_adjoints_projection_settings[j] = old_adjoint_vector_projection_setting;

          // Set the preservation to true for the upcoming reinits
          system.set_vector_preservation(adjoint_vector_name, true);
        }
    }

  // And we'll need to temporarily change solution projection settings
  bool old_projection_setting;
  old_projection_setting = system.project_solution_on_reinit();

  // Make sure the solution is projected when we refine the mesh
  system.project_solution_on_reinit() = true;

  // And we need to make sure we dont reapply constraints after refining the mesh
  bool old_project_with_constraints_setting;
  old_project_with_constraints_setting = system.get_project_with_constraints();

  system.set_project_with_constraints(false);

  // And it'll be best to avoid any repartitioning
  std::unique_ptr<Partitioner> old_partitioner(mesh.partitioner().release());

  // And we can't allow any renumbering
  const bool old_renumbering_setting = mesh.allow_renumbering();
  mesh.allow_renumbering(false);

  // Use a non-standard solution vector if necessary
  if (solution_vector && solution_vector != system.solution.get())
    {
      NumericVector<Number> * newsol =
        const_cast<NumericVector<Number> *> (solution_vector);
      newsol->swap(*system.solution);
      system.update();
    }

  // Resize the error_per_cell vector to be
  // the number of elements, initialized to 0.
  error_per_cell.clear();
  error_per_cell.resize (mesh.max_elem_id(), 0.);

#ifndef NDEBUG
  // These variables are only used in assertions later so
  // avoid declaring them unless asserts are active.
  const dof_id_type n_coarse_elem = mesh.n_active_elem();

  dof_id_type local_dof_bearing_nodes = 0;
  const unsigned int sysnum = system.number();
  for (const auto * node : mesh.local_node_ptr_range())
    for (unsigned int v=0, nvars = node->n_vars(sysnum);
         v != nvars; ++v)
      if (node->n_comp(sysnum, v))
        {
          local_dof_bearing_nodes++;
          break;
        }
#endif // NDEBUG

  // Uniformly refine the mesh
  MeshRefinement mesh_refinement(mesh);

  // We only need to worry about Galerkin orthogonality if we
  // are estimating discretization error in a single model setting
  {
    const bool swapping_adjoint_physics = _adjoint_evaluation_physics;
    if(!swapping_adjoint_physics)
      libmesh_assert (number_h_refinements > 0 || number_p_refinements > 0);
  }

  // FIXME: this may break if there is more than one System
  // on this mesh but estimate_error was still called instead of
  // estimate_errors
  for (unsigned int i = 0; i != number_h_refinements; ++i)
    {
      mesh_refinement.uniformly_refine(1);
      es.reinit();
    }

  for (unsigned int i = 0; i != number_p_refinements; ++i)
    {
      mesh_refinement.uniformly_p_refine(1);
      es.reinit();
    }

  // Copy the projected coarse grid solutions, which will be
  // overwritten by solve()
  std::vector<std::unique_ptr<NumericVector<Number>>> coarse_adjoints;
  for (auto j : make_range(system.n_qois()))
    {
      if (_qoi_set.has_index(j))
        {
          auto coarse_adjoint = NumericVector<Number>::build(mesh.comm());

          // Can do "fast" init since we're overwriting this in a sec
          coarse_adjoint->init(system.get_adjoint_solution(j),
                               /* fast = */ true);

          *coarse_adjoint = system.get_adjoint_solution(j);

          coarse_adjoints.emplace_back(std::move(coarse_adjoint));
        }
      else
        coarse_adjoints.emplace_back(nullptr);
    }

  // Next, we are going to build up the residual for evaluating the
  // error estimate.

  // If the residual physics pointer is not null, use it when
  // evaluating here.
  {
    if (_residual_evaluation_physics)
      dynamic_cast<DifferentiableSystem &>(system).push_physics(*_residual_evaluation_physics);

    // Rebuild the rhs with the projected primal solution, do not apply constraints
    system.assembly(true, false, false, true);

    // Restore the original physics if needed
    if (_residual_evaluation_physics)
      dynamic_cast<DifferentiableSystem &>(system).pop_physics();
  }

  NumericVector<Number> & projected_residual = (dynamic_cast<ExplicitSystem &>(system)).get_vector("RHS Vector");
  projected_residual.close();

  if (_adjoint_evaluation_physics)
    dynamic_cast<DifferentiableSystem &>(system).push_physics(*_adjoint_evaluation_physics);

  // Solve the adjoint problem(s) on the refined FE space
  // The matrix will be reassembled again because we have refined the mesh
  // If we have no h or p refinements, no need to solve for a fine adjoint
  if(number_h_refinements > 0 || number_p_refinements > 0)
    system.adjoint_solve(_qoi_set);

  // Swap back if needed, recall that _adjoint_evaluation_physics now holds the pointer
  // to the pre-swap physics
  if (_adjoint_evaluation_physics)
    dynamic_cast<DifferentiableSystem &>(system).pop_physics();

  // Now that we have the refined adjoint solution and the projected primal solution,
  // we first compute the global QoI error estimate

  // Resize the computed_global_QoI_errors vector to hold the error estimates for each QoI
  computed_global_QoI_errors.resize(system.n_qois());

  // Loop over all the adjoint solutions and get the QoI error
  // contributions from all of them.  While we're looping anyway we'll
  // pull off the coarse adjoints
  for (auto j : make_range(system.n_qois()))
    {
      // Skip this QoI if not in the QoI Set
      if (_qoi_set.has_index(j))
        {

          // If the adjoint solution has heterogeneous dirichlet
          // values, then to get a proper error estimate here we need
          // to subtract off a coarse grid lift function.
          // |Q(u) - Q(u^h)| = |R([u^h]+, z^h+ - [L]+)| + HOT
          if(system.get_dof_map().has_adjoint_dirichlet_boundaries(j))
            {
              // Need to create a string with current loop index to retrieve
              // the correct vector from the liftvectors map
              std::ostringstream liftfunc_name;
              liftfunc_name << "adjoint_lift_function" << j;

              // Subtract off the corresponding lift vector from the adjoint solution
              system.get_adjoint_solution(j) -= system.get_vector(liftfunc_name.str());

              // Now evaluate R(u^h, z^h+ - lift)
              computed_global_QoI_errors[j] = projected_residual.dot(system.get_adjoint_solution(j));

              // Add the lift back to get back the adjoint
              system.get_adjoint_solution(j) += system.get_vector(liftfunc_name.str());

            }
          else
            {
              // Usual dual weighted residual error estimate
              // |Q(u) - Q(u^h)| = |R([u^h]+, z^h+)| + HOT
              computed_global_QoI_errors[j] = projected_residual.dot(system.get_adjoint_solution(j));
            }

        }
    }

  // We are all done with Dirichlet lift vectors and they should be removed, lest we run into I/O issues later
  for (auto j : make_range(system.n_qois()))
    {
      // Skip this QoI if not in the QoI Set
      if (_qoi_set.has_index(j))
        {
          // Lifts are created only for adjoint dirichlet QoIs
          if(system.get_dof_map().has_adjoint_dirichlet_boundaries(j))
            {
              // Need to create a string with current loop index to retrieve
              // the correct vector from the liftvectors map
              std::ostringstream liftfunc_name;
              liftfunc_name << "adjoint_lift_function" << j;

              // Remove the lift vector from system since we did not write it to file and it cannot be retrieved
              system.remove_vector(liftfunc_name.str());
            }
        }
    }

  // Done with the global error estimates, now construct the element wise error indicators

  // To get a better element wise breakdown of the error estimate,
  // we subtract off a coarse representation of the adjoint solution.
  // |Q(u) - Q(u^h)| = |R([u^h]+, z^h+ - [z^h]+)|
  // This remains valid for all combinations of heterogenous adjoint bcs and
  // stabilized/non-stabilized formulations, except for the case where we not using a
  // heterogenous adjoint bc and have a stabilized formulation.
  // Then, R(u^h_s, z^h_s)  != 0 (no Galerkin orthogonality w.r.t the non-stabilized residual)
  for (auto j : make_range(system.n_qois()))
    {
      // Skip this QoI if not in the QoI Set
      if (_qoi_set.has_index(j))
        {
          // If we have a nullptr residual evaluation physics pointer, we
          // assume the user's formulation is consistent from mesh to
          // mesh, so we have Galerkin orthogonality and we can get
          // better indicator results by subtracting a coarse adjoint.

          // If we have a residual evaluation physics pointer, but we
          // also have heterogeneous adjoint dirichlet boundaries,
          // then we have to subtract off *some* lift function for
          // consistency, and we choose the coarse adjoint in lieu of
          // having any better ideas.

          // If we have a residual evaluation physics pointer and we
          // have homogeneous adjoint dirichlet boundaries, then we
          // don't have to subtract off anything, and with stabilized
          // formulations we get the best results if we don't.
          if(system.get_dof_map().has_adjoint_dirichlet_boundaries(j)
             || !_residual_evaluation_physics)
            {
              // z^h+ -> z^h+ - [z^h]+
              system.get_adjoint_solution(j) -= *coarse_adjoints[j];
            }
        }
    }

  // We ought to account for 'spill-over' effects while computing the
  // element error indicators This happens because the same dof is
  // shared by multiple elements, one way of mitigating this is to
  // scale the contribution from each dof by the number of elements it
  // belongs to We first obtain the number of elements each node
  // belongs to

  // A map that relates a node id to an int that will tell us how many elements it is a node of
  std::unordered_map<dof_id_type, unsigned int> shared_element_count;

  // To fill this map, we will loop over elements, and then in each element, we will loop
  // over the nodes each element contains, and then query it for the number of coarse
  // grid elements it was a node of

  // Keep track of which nodes we have already dealt with
  std::unordered_set<dof_id_type> processed_node_ids;

  // We will be iterating over all the active elements in the fine mesh that live on
  // this processor.
  for (const auto & elem : mesh.active_local_element_ptr_range())
    for (const Node & node : elem->node_ref_range())
      {
        // Get the id of this node
        dof_id_type node_id = node.id();

        // If we haven't already processed this node, do so now
        if (processed_node_ids.find(node_id) == processed_node_ids.end())
          {
            // Declare a neighbor_set to be filled by the find_point_neighbors
            std::set<const Elem *> fine_grid_neighbor_set;

            // Call find_point_neighbors to fill the neighbor_set
            elem->find_point_neighbors(node, fine_grid_neighbor_set);

            // A vector to hold the coarse grid parents neighbors
            std::vector<dof_id_type> coarse_grid_neighbors;

            // Loop over all the fine neighbors of this node
            for (const auto & fine_elem : fine_grid_neighbor_set)
              {
                // Find the element id for the corresponding coarse grid element
                const Elem * coarse_elem = fine_elem;
                for (unsigned int j = 0; j != number_h_refinements; ++j)
                  {
                    libmesh_assert (coarse_elem->parent());

                    coarse_elem = coarse_elem->parent();
                  }

                // Loop over the existing coarse neighbors and check if this one is
                // already in there
                const dof_id_type coarse_id = coarse_elem->id();
                std::size_t j = 0;

                // If the set already contains this element break out of the loop
                for (std::size_t cgns = coarse_grid_neighbors.size(); j != cgns; j++)
                  if (coarse_grid_neighbors[j] == coarse_id)
                    break;

                // If we didn't leave the loop even at the last element,
                // this is a new neighbour, put in the coarse_grid_neighbor_set
                if (j == coarse_grid_neighbors.size())
                  coarse_grid_neighbors.push_back(coarse_id);
              } // End loop over fine neighbors

            // Set the shared_neighbour index for this node to the
            // size of the coarse grid neighbor set
            shared_element_count[node_id] =
              cast_int<unsigned int>(coarse_grid_neighbors.size());

            // Add this node to processed_node_ids vector
            processed_node_ids.insert(node_id);
          } // End if not processed node
      } // End loop over nodes

  // Get a DoF map, will be used to get the nodal dof_indices for each element
  DofMap & dof_map = system.get_dof_map();

  // The global DOF indices, we will use these later on when we compute the element wise indicators
  std::vector<dof_id_type> dof_indices;

  // Localize the global rhs and adjoint solution vectors (which might be shared on multiple processors) onto a
  // local ghosted vector, this ensures each processor has all the dof_indices to compute an error indicator for
  // an element it owns
  std::unique_ptr<NumericVector<Number>> localized_projected_residual = NumericVector<Number>::build(system.comm());
  localized_projected_residual->init(system.n_dofs(), system.n_local_dofs(), system.get_dof_map().get_send_list(), false, GHOSTED);
  projected_residual.localize(*localized_projected_residual, system.get_dof_map().get_send_list());

  // Each adjoint solution will also require ghosting; for efficiency we'll reuse the same memory
  std::unique_ptr<NumericVector<Number>> localized_adjoint_solution = NumericVector<Number>::build(system.comm());
  localized_adjoint_solution->init(system.n_dofs(), system.n_local_dofs(), system.get_dof_map().get_send_list(), false, GHOSTED);

  // We will loop over each adjoint solution, localize that adjoint
  // solution and then loop over local elements
  for (auto i : make_range(system.n_qois()))
    {
      // Skip this QoI if not in the QoI Set
      if (_qoi_set.has_index(i))
        {
          // Get the weight for the current QoI
          Real error_weight = _qoi_set.weight(i);

          (system.get_adjoint_solution(i)).localize(*localized_adjoint_solution, system.get_dof_map().get_send_list());

          // Loop over elements
          for (const auto & elem : mesh.active_local_element_ptr_range())
            {
              // Go up number_h_refinements levels up to find the coarse parent
              const Elem * coarse = elem;

              for (unsigned int j = 0; j != number_h_refinements; ++j)
                {
                  libmesh_assert (coarse->parent());

                  coarse = coarse->parent();
                }

              const dof_id_type e_id = coarse->id();

              // Get the local to global degree of freedom maps for this element
              dof_map.dof_indices (elem, dof_indices);

              // We will have to manually do the dot products.
              Number local_contribution = 0.;

              // Sum the contribution to the error indicator for each element from the current QoI
              for (const auto & dof : dof_indices)
                local_contribution += (*localized_projected_residual)(dof) * (*localized_adjoint_solution)(dof);

              // Multiply by the error weight for this QoI
              local_contribution *= error_weight;

              // FIXME: we're throwing away information in the
              // --enable-complex case
              error_per_cell[e_id] += static_cast<ErrorVectorReal>
                (std::abs(local_contribution));

            } // End loop over elements
        } // End if belong to QoI set
    } // End loop over QoIs

  // Don't bother projecting the solution; we'll restore from backup
  // after coarsening
  system.project_solution_on_reinit() = false;

  // Uniformly coarsen the mesh, without projecting the solution
  // Only need to do this if we are estimating discretization error
  // with a single physics residual
  libmesh_assert (_adjoint_evaluation_physics ||
                  number_h_refinements > 0 || number_p_refinements > 0);

  for (unsigned int i = 0; i != number_h_refinements; ++i)
    {
      mesh_refinement.uniformly_coarsen(1);
      // FIXME - should the reinits here be necessary? - RHS
      es.reinit();
    }

  for (unsigned int i = 0; i != number_p_refinements; ++i)
    {
      mesh_refinement.uniformly_p_coarsen(1);
      es.reinit();
    }

  // We should have the same number of active elements as when we started,
  // but not necessarily the same number of elements since reinit() doesn't
  // always call contract()
  libmesh_assert_equal_to (n_coarse_elem, mesh.n_active_elem());

  // We should have the same number of dof-bearing nodes as when we
  // started
#ifndef NDEBUG
  dof_id_type final_local_dof_bearing_nodes = 0;
  for (const auto * node : mesh.local_node_ptr_range())
    for (unsigned int v=0, nvars = node->n_vars(sysnum);
         v != nvars; ++v)
      if (node->n_comp(sysnum, v))
        {
          final_local_dof_bearing_nodes++;
          break;
        }
  libmesh_assert_equal_to (local_dof_bearing_nodes,
                           final_local_dof_bearing_nodes);
#endif // NDEBUG

  // Restore old solutions and clean up the heap
  system.project_solution_on_reinit() = old_projection_setting;
  system.set_project_with_constraints(old_project_with_constraints_setting);

  // Restore the adjoint vector preservation settings
  for (auto j : make_range(system.n_qois()))
    {
      if (_qoi_set.has_index(j))
        {
          auto adjoint_vector_name = system.vector_name(system.get_adjoint_solution(j));
          system.set_vector_preservation(adjoint_vector_name, old_adjoints_projection_settings[j]);
        }
    }

  // Restore the coarse solution vectors and delete their copies
  *system.solution = *coarse_solution;
  *system.current_local_solution = *coarse_local_solution;

  for (const auto & pr : as_range(system.vectors_begin(), system.vectors_end()))
    {
      // The (string) name of this vector
      const std::string & var_name = pr.first;

      // If it's a vector we already had (and not a newly created
      // vector like an adjoint rhs), we need to restore it.
      if (auto it = coarse_vectors.find(var_name);
          it != coarse_vectors.end())
        {
          NumericVector<Number> * coarsevec = it->second.get();
          system.get_vector(var_name) = *coarsevec;

          coarsevec->clear();
        }
    }

  // Restore old partitioner and renumbering settings
  mesh.partitioner().reset(old_partitioner.release());
  mesh.allow_renumbering(old_renumbering_setting);

  // Finally sum the vector of estimated error values.
  this->reduce_error(error_per_cell, system.comm());

  // We don't take a square root here; this is a goal-oriented
  // estimate not a Hilbert norm estimate.
} // end estimate_error function

}// namespace libMesh

#endif // #ifdef LIBMESH_ENABLE_AMR
