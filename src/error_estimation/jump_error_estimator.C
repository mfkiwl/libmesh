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


// Local Includes
#include "libmesh/libmesh_common.h"
#include "libmesh/jump_error_estimator.h"
#include "libmesh/dof_map.h"
#include "libmesh/elem.h"
#include "libmesh/error_vector.h"
#include "libmesh/fe_base.h"
#include "libmesh/fe_interface.h"
#include "libmesh/fem_context.h"
#include "libmesh/libmesh_logging.h"
#include "libmesh/mesh_base.h"
#include "libmesh/quadrature_gauss.h"
#include "libmesh/system.h"
#include "libmesh/dense_vector.h"
#include "libmesh/numeric_vector.h"
#include "libmesh/int_range.h"

// C++ Includes
#include <algorithm> // for std::fill
#include <cstdlib> // *must* precede <cmath> for proper std:abs() on PGI, Sun Studio CC
#include <cmath>    // for sqrt
#include <memory>


namespace libMesh
{

//-----------------------------------------------------------------
// JumpErrorEstimator implementations
void JumpErrorEstimator::init_context (FEMContext &)
{
  // Derived classes are *supposed* to rederive this
  libmesh_deprecated();
}



void JumpErrorEstimator::estimate_error (const System & system,
                                         ErrorVector & error_per_cell,
                                         const NumericVector<Number> * solution_vector,
                                         bool estimate_parent_error)
{
  LOG_SCOPE("estimate_error()", "JumpErrorEstimator");

  /**
   * Conventions for assigning the direction of the normal:
   *
   * - e & f are global element ids
   *
   * Case (1.) Elements are at the same level, e<f
   * Compute the flux jump on the face and
   * add it as a contribution to error_per_cell[e]
   * and error_per_cell[f]
   *
   *  ----------------------
   * |           |          |
   * |           |    f     |
   * |           |          |
   * |    e      |---> n    |
   * |           |          |
   * |           |          |
   *  ----------------------
   *
   *
   * Case (2.) The neighbor is at a higher level.
   * Compute the flux jump on e's face and
   * add it as a contribution to error_per_cell[e]
   * and error_per_cell[f]
   *
   *  ----------------------
   * |     |     |          |
   * |     |  e  |---> n    |
   * |     |     |          |
   * |-----------|    f     |
   * |     |     |          |
   * |     |     |          |
   * |     |     |          |
   *  ----------------------
   */

  // This parameter is not used when !LIBMESH_ENABLE_AMR.
  libmesh_ignore(estimate_parent_error);

  // The current mesh
  const MeshBase & mesh = system.get_mesh();

  // The number of variables in the system
  const unsigned int n_vars = system.n_vars();

  // The DofMap for this system
#ifdef LIBMESH_ENABLE_AMR
  const DofMap & dof_map = system.get_dof_map();
#endif

  // Resize the error_per_cell vector to be
  // the number of elements, initialize it to 0.
  error_per_cell.resize (mesh.max_elem_id());
  std::fill (error_per_cell.begin(), error_per_cell.end(), 0.);

  // Declare a vector of floats which is as long as
  // error_per_cell above, and fill with zeros.  This vector will be
  // used to keep track of the number of edges (faces) on each active
  // element which are either:
  // 1) an internal edge
  // 2) an edge on a Neumann boundary for which a boundary condition
  //    function has been specified.
  // The error estimator can be scaled by the number of flux edges (faces)
  // which the element actually has to obtain a more uniform measure
  // of the error.  Use floats instead of ints since in case 2 (above)
  // f gets 1/2 of a flux face contribution from each of his
  // neighbors
  std::vector<float> n_flux_faces;
  if (scale_by_n_flux_faces)
    n_flux_faces.resize(error_per_cell.size(), 0);

  // Prepare current_local_solution to localize a non-standard
  // solution vector if necessary
  if (solution_vector && solution_vector != system.solution.get())
    {
      NumericVector<Number> * newsol =
        const_cast<NumericVector<Number> *>(solution_vector);
      System & sys = const_cast<System &>(system);
      newsol->swap(*sys.solution);
      sys.update();
    }

  // We don't use full elem_jacobian or subjacobians here.
  fine_context = std::make_unique<FEMContext>
    (system, nullptr, /* allocate_local_matrices = */ false);
  coarse_context = std::make_unique<FEMContext>
    (system, nullptr, /* allocate_local_matrices = */ false);

  // Don't overintegrate - we're evaluating differences of FE values,
  // not products of them.
  if (this->use_unweighted_quadrature_rules)
    fine_context->use_unweighted_quadrature_rules(system.extra_quadrature_order);

  // Loop over all the variables we've been requested to find jumps in, to
  // pre-request
  for (var=0; var<n_vars; var++)
    {
      // Skip variables which aren't part of our norm,
      // as well as SCALAR variables, which have no jumps
      if (error_norm.weight(var) == 0.0 ||
          system.variable_type(var).family == SCALAR)
        continue;

      // FIXME: Need to generalize this to vector-valued elements. [PB]
      FEBase * side_fe = nullptr;

      const std::set<unsigned char> & elem_dims =
        fine_context->elem_dimensions();

      for (const auto & dim : elem_dims)
        {
          fine_context->get_side_fe( var, side_fe, dim );

          side_fe->get_xyz();
        }
    }

  this->init_context(*fine_context);
  this->init_context(*coarse_context);

  // If we're integrating jumps across mesh slits, then we'll need a
  // point locator to find slits, and we'll need to integrate point by
  // point on sides.
  std::unique_ptr<PointLocatorBase> point_locator;
  std::unique_ptr<const Elem> side_ptr;

  if (integrate_slits)
    point_locator = mesh.sub_point_locator();

  // Iterate over all the active elements in the mesh
  // that live on this processor.
  for (const auto & e : mesh.active_local_element_ptr_range())
    {
      const dof_id_type e_id = e->id();

#ifdef LIBMESH_ENABLE_AMR

      if (e->infinite())
      {
         libmesh_warning("Warning: Jumps on the border of infinite elements are ignored."
                         << std::endl);
         continue;
      }

      // See if the parent of element e has been examined yet;
      // if not, we may want to compute the estimator on it
      const Elem * parent = e->parent();

      // We only can compute and only need to compute on
      // parents with all active children
      bool compute_on_parent = true;
      if (!parent || !estimate_parent_error)
        compute_on_parent = false;
      else
        for (auto & child : parent->child_ref_range())
          if (!child.active())
            compute_on_parent = false;

      if (compute_on_parent &&
          !error_per_cell[parent->id()])
        {
          // Compute a projection onto the parent
          DenseVector<Number> Uparent;
          FEBase::coarsened_dof_values
            (*(system.solution), dof_map, parent, Uparent, false);

          // Loop over the neighbors of the parent
          for (auto n_p : parent->side_index_range())
            {
              if (parent->neighbor_ptr(n_p) != nullptr) // parent has a neighbor here
                {
                  // Find the active neighbors in this direction
                  std::vector<const Elem *> active_neighbors;
                  parent->neighbor_ptr(n_p)->
                    active_family_tree_by_neighbor(active_neighbors,
                                                   parent);
                  // Compute the flux to each active neighbor
                  for (std::size_t a=0,
                        n_active_neighbors = active_neighbors.size();
                       a != n_active_neighbors; ++a)
                    {
                      const Elem * f = active_neighbors[a];

                      if (f ->infinite()) // don't take infinite elements into account
                         continue;

                      // FIXME - what about when f->level <
                      // parent->level()??
                      if (f->level() >= parent->level())
                        {
                          fine_context->pre_fe_reinit(system, f);
                          coarse_context->pre_fe_reinit(system, parent);
                          libmesh_assert_equal_to
                            (coarse_context->get_elem_solution().size(),
                             Uparent.size());
                          coarse_context->get_elem_solution() = Uparent;

                          this->reinit_sides();

                          // Loop over all significant variables in the system
                          for (var=0; var<n_vars; var++)
                            if (error_norm.weight(var) != 0.0 &&
                                system.variable_type(var).family != SCALAR)
                              {
                                this->internal_side_integration();

                                error_per_cell[fine_context->get_elem().id()] +=
                                  static_cast<ErrorVectorReal>(fine_error);
                                error_per_cell[coarse_context->get_elem().id()] +=
                                  static_cast<ErrorVectorReal>(coarse_error);
                              }

                          // Keep track of the number of internal flux
                          // sides found on each element
                          if (scale_by_n_flux_faces)
                            {
                              n_flux_faces[fine_context->get_elem().id()]++;
                              n_flux_faces[coarse_context->get_elem().id()] +=
                                this->coarse_n_flux_faces_increment();
                            }
                        }
                    }
                }
              else if (integrate_boundary_sides)
                {
                  fine_context->pre_fe_reinit(system, parent);
                  libmesh_assert_equal_to
                    (fine_context->get_elem_solution().size(),
                     Uparent.size());
                  fine_context->get_elem_solution() = Uparent;
                  fine_context->side = cast_int<unsigned char>(n_p);
                  fine_context->side_fe_reinit();

                  // If we find a boundary flux for any variable,
                  // let's just count it as a flux face for all
                  // variables.  Otherwise we'd need to keep track of
                  // a separate n_flux_faces and error_per_cell for
                  // every single var.
                  bool found_boundary_flux = false;

                  for (var=0; var<n_vars; var++)
                    if (error_norm.weight(var) != 0.0 &&
                        system.variable_type(var).family != SCALAR)
                      {
                        if (this->boundary_side_integration())
                          {
                            error_per_cell[fine_context->get_elem().id()] +=
                              static_cast<ErrorVectorReal>(fine_error);
                            found_boundary_flux = true;
                          }
                      }

                  if (scale_by_n_flux_faces && found_boundary_flux)
                    n_flux_faces[fine_context->get_elem().id()]++;
                }
            }
        }
#endif // #ifdef LIBMESH_ENABLE_AMR

      // If we do any more flux integration, e will be the fine element
      fine_context->pre_fe_reinit(system, e);

      // Loop over the neighbors of element e
      for (auto n_e : e->side_index_range())
        {
          if ((e->neighbor_ptr(n_e) != nullptr) ||
              integrate_boundary_sides)
            {
              fine_context->side = cast_int<unsigned char>(n_e);
              fine_context->side_fe_reinit();
            }

          // e is not on the boundary (infinite elements are treated as boundary)
          if (e->neighbor_ptr(n_e) != nullptr
              && !e->neighbor_ptr(n_e) ->infinite())
            {

              const Elem * f           = e->neighbor_ptr(n_e);
              const dof_id_type f_id = f->id();

              // Compute flux jumps if we are in case 1 or case 2.
              if ((f->active() && (f->level() == e->level()) && (e_id < f_id))
                  || (f->level() < e->level()))
                {
                  // f is now the coarse element
                  coarse_context->pre_fe_reinit(system, f);

                  this->reinit_sides();

                  // Loop over all significant variables in the system
                  for (var=0; var<n_vars; var++)
                    if (error_norm.weight(var) != 0.0 &&
                        system.variable_type(var).family != SCALAR)
                      {
                        this->internal_side_integration();

                        error_per_cell[fine_context->get_elem().id()] +=
                          static_cast<ErrorVectorReal>(fine_error);
                        error_per_cell[coarse_context->get_elem().id()] +=
                          static_cast<ErrorVectorReal>(coarse_error);
                      }

                  // Keep track of the number of internal flux
                  // sides found on each element
                  if (scale_by_n_flux_faces)
                    {
                      n_flux_faces[fine_context->get_elem().id()]++;
                      n_flux_faces[coarse_context->get_elem().id()] +=
                        this->coarse_n_flux_faces_increment();
                    }
                } // end if (case1 || case2)
            } // if (e->neighbor(n_e) != nullptr)

          // e might not have a neighbor_ptr, but might still have
          // another element sharing its side.  This can happen in a
          // mesh where solution continuity is maintained via nodal
          // constraint rows.
          else if (integrate_slits)
            {
              side_ptr = e->build_side_ptr(n_e);
              std::set<const Elem *> candidate_elements;
              (*point_locator)(side_ptr->vertex_average(), candidate_elements);

              // We should have at least found ourselves...
              libmesh_assert(candidate_elements.count(e));

              // If we only found ourselves, this probably isn't a
              // slit; we don't yet support meshes so non-conforming
              // as to have overlap of part of an element side without
              // overlap of its center.
              if (candidate_elements.size() < 2)
                continue;

              FEType hardest_fe_type = fine_context->find_hardest_fe_type();

              auto dim = e->dim();

              auto side_qrule =
                hardest_fe_type.unweighted_quadrature_rule
                (dim-1, system.extra_quadrature_order);
              auto side_fe = FEAbstract::build(dim, hardest_fe_type);
              side_fe->attach_quadrature_rule(side_qrule.get());
              const std::vector<Point> & qface_point = side_fe->get_xyz();
              side_fe->reinit(e, n_e);

              for (auto qp : make_range(side_qrule->n_points()))
                {
                  const Point p = qface_point[qp];
                  const std::vector<Point> qp_pointvec(1, p);
                  std::set<const Elem *> side_elements;
                  (*point_locator)(side_ptr->vertex_average(), side_elements);

                  // If we have multiple neighbors meeting here we'll just
                  // take weighted jumps from all of them.
                  //
                  // We'll also do integrations from both sides of slits,
                  // rather than try to figure out a disambiguation rule
                  // that makes sense for non-conforming slits in general.
                  // This means we want an extra factor of 0.5 on the
                  // integrals to compensate for doubling them.
                  const std::size_t n_neighbors = side_elements.size() - 1;
                  const Real neighbor_frac = Real(1)/n_neighbors;

                  const std::vector<Real>
                    qp_weightvec(1, neighbor_frac * side_qrule->w(qp));

                  for (const Elem * f : side_elements)
                    {
                      if (f == e)
                        continue;

                      coarse_context->pre_fe_reinit(system, f);
                      fine_context->pre_fe_reinit(system, e);
                      std::vector<Point> qp_coarse, qp_fine;
                      for (unsigned int v=0; v<n_vars; v++)
                        if (error_norm.weight(v) != 0.0 &&
                            fine_context->get_system().variable_type(v).family != SCALAR)
                          {
                            FEBase * coarse_fe = coarse_context->get_side_fe(v, dim);
                            if (qp_coarse.empty())
                              FEMap::inverse_map (dim, f, qp_pointvec, qp_coarse);
                            coarse_fe->reinit(f, &qp_coarse, &qp_weightvec);
                            FEBase * fine_fe = fine_context->get_side_fe(v, dim);
                            if (qp_fine.empty())
                              FEMap::inverse_map (dim, e, qp_pointvec, qp_fine);
                            fine_fe->reinit(e, &qp_fine, &qp_weightvec);
                          }

                      // Loop over all significant variables in the system
                      for (var=0; var<n_vars; var++)
                        if (error_norm.weight(var) != 0.0 &&
                            system.variable_type(var).family != SCALAR)
                          {
                            this->internal_side_integration();

                            error_per_cell[fine_context->get_elem().id()] +=
                              static_cast<ErrorVectorReal>(fine_error);
                            error_per_cell[coarse_context->get_elem().id()] +=
                              static_cast<ErrorVectorReal>(coarse_error);
                          }
                    }
                }
            }

          // Otherwise, e is on the boundary.  If it happens to
          // be on a Dirichlet boundary, we need not do anything.
          // On the other hand, if e is on a Neumann (flux) boundary
          // with grad(u).n = g, we need to compute the additional residual
          // (h * \int |g - grad(u_h).n|^2 dS)^(1/2).
          // We can only do this with some knowledge of the boundary
          // conditions, i.e. the user must have attached an appropriate
          // BC function.
          else if (integrate_boundary_sides)
            {
              if (integrate_slits)
                libmesh_not_implemented();

              bool found_boundary_flux = false;

              for (var=0; var<n_vars; var++)
                if (error_norm.weight(var) != 0.0 &&
                    system.variable_type(var).family != SCALAR)
                  if (this->boundary_side_integration())
                    {
                      error_per_cell[fine_context->get_elem().id()] +=
                        static_cast<ErrorVectorReal>(fine_error);
                      found_boundary_flux = true;
                    }

              if (scale_by_n_flux_faces && found_boundary_flux)
                n_flux_faces[fine_context->get_elem().id()]++;
            } // end if (e->neighbor_ptr(n_e) == nullptr)
        } // end loop over neighbors
    } // End loop over active local elements


  // Each processor has now computed the error contributions
  // for its local elements.  We need to sum the vector
  // and then take the square-root of each component.  Note
  // that we only need to sum if we are running on multiple
  // processors, and we only need to take the square-root
  // if the value is nonzero.  There will in general be many
  // zeros for the inactive elements.

  // First sum the vector of estimated error values
  this->reduce_error(error_per_cell, system.comm());

  // Compute the square-root of each component.
  for (auto i : index_range(error_per_cell))
    if (error_per_cell[i] != 0.)
      error_per_cell[i] = std::sqrt(error_per_cell[i]);


  if (this->scale_by_n_flux_faces)
    {
      // Sum the vector of flux face counts
      this->reduce_error(n_flux_faces, system.comm());

      // Sanity check: Make sure the number of flux faces is
      // always an integer value
#ifdef DEBUG
      for (const auto & val : n_flux_faces)
        libmesh_assert_equal_to (val, static_cast<float>(static_cast<unsigned int>(val)));
#endif

      // Scale the error by the number of flux faces for each element
      for (auto i : index_range(n_flux_faces))
        {
          if (n_flux_faces[i] == 0.0) // inactive or non-local element
            continue;

          error_per_cell[i] /= static_cast<ErrorVectorReal>(n_flux_faces[i]);
        }
    }

  // If we used a non-standard solution before, now is the time to fix
  // the current_local_solution
  if (solution_vector && solution_vector != system.solution.get())
    {
      NumericVector<Number> * newsol =
        const_cast<NumericVector<Number> *>(solution_vector);
      System & sys = const_cast<System &>(system);
      newsol->swap(*sys.solution);
      sys.update();
    }
}



void
JumpErrorEstimator::reinit_sides ()
{
  fine_context->side_fe_reinit();

  unsigned short dim = fine_context->get_elem().dim();
  libmesh_assert_equal_to(dim, coarse_context->get_elem().dim());

  FEBase * fe_fine = nullptr;
  fine_context->get_side_fe( 0, fe_fine, dim );

  // Get the physical locations of the fine element quadrature points
  std::vector<Point> qface_point = fe_fine->get_xyz();

  // Find the master quadrature point locations on the coarse element
  FEBase * fe_coarse = nullptr;
  coarse_context->get_side_fe( 0, fe_coarse, dim );

  std::vector<Point> qp_coarse;

  FEMap::inverse_map (coarse_context->get_elem().dim(),
                      &coarse_context->get_elem(), qface_point,
                      qp_coarse);

  // The number of variables in the system
  const unsigned int n_vars = fine_context->n_vars();

  // Calculate all coarse element shape functions at those locations
  for (unsigned int v=0; v<n_vars; v++)
    if (error_norm.weight(v) != 0.0 &&
        fine_context->get_system().variable_type(v).family != SCALAR)
      {
        coarse_context->get_side_fe( v, fe_coarse, dim );
        fe_coarse->reinit (&coarse_context->get_elem(), &qp_coarse);
      }
}



float JumpErrorEstimator::coarse_n_flux_faces_increment ()
{
  // Keep track of the number of internal flux sides found on each
  // element
  unsigned short dim = coarse_context->get_elem().dim();

  const unsigned int divisor =
    1 << (dim-1)*(fine_context->get_elem().level() -
                  coarse_context->get_elem().level());

  // With a difference of n levels between fine and coarse elements,
  // we compute a fractional flux face for the coarse element by adding:
  // 1/2^n in 2D
  // 1/4^n in 3D
  // each time.  This code will get hit 2^n times in 2D and 4^n
  // times in 3D so that the final flux face count for the coarse
  // element will be an integer value.

  return 1.0f / static_cast<float>(divisor);
}

} // namespace libMesh
