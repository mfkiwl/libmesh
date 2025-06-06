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



// <h1>Adjoints Example 2 - Laplace Equation in the L-Shaped Domain with
// Adjoint based sensitivity</h1>
// \author Roy Stogner
// \date 2003
//
// This example solves the Laplace equation on the classic "L-shaped"
// domain with adaptive mesh refinement. The exact solution is
// u(r,\theta) = r^{2/3} * \sin ( (2/3) * \theta). We scale this
// exact solution by a combination of parameters, (\alpha_{1} + 2
// *\alpha_{2}) to get u = (\alpha_{1} + 2 *\alpha_{2}) * r^{2/3} *
// \sin ( (2/3) * \theta), which again satisfies the Laplace
// Equation. We define the Quantity of Interest in element_qoi.C, and
// compute the sensitivity of the QoI to \alpha_{1} and \alpha_{2}
// using the adjoint sensitivity method. Since we use the adjoint
// capabilities of libMesh in this example, we use the DiffSystem
// framework. This file (main.C) contains the declaration of mesh and
// equation system objects, L-shaped.C contains the assembly of the
// system, element_qoi_derivative.C contains
// the RHS for the adjoint system. Postprocessing to compute the the
// QoIs is done in element_qoi.C

// The initial mesh contains three QUAD9 elements which represent the
// standard quadrants I, II, and III of the domain [-1,1]x[-1,1],
// i.e.
// Element 0: [-1,0]x[ 0,1]
// Element 1: [ 0,1]x[ 0,1]
// Element 2: [-1,0]x[-1,0]
// The mesh is provided in the standard libMesh ASCII format file
// named "lshaped.xda".  In addition, an input file named "general.in"
// is provided which allows the user to set several parameters for
// the solution so that the problem can be re-run without a
// re-compile.  The solution technique employed is to have a
// refinement loop with a linear (forward and adjoint) solve inside followed by a
// refinement of the grid and projection of the solution to the new grid
// In the final loop iteration, there is no additional
// refinement after the solve.  In the input file "general.in", the variable
// "max_adaptivesteps" controls the number of refinement steps, and
// "refine_fraction" / "coarsen_fraction" determine the number of
// elements which will be refined / coarsened at each step.

// C++ includes
#include <iostream>
#include <iomanip>
#include <memory>

// General libMesh includes
#include "libmesh/eigen_sparse_linear_solver.h"
#include "libmesh/enum_solver_package.h"
#include "libmesh/enum_solver_type.h"
#include "libmesh/equation_systems.h"
#include "libmesh/error_vector.h"
#include "libmesh/mesh.h"
#include "libmesh/mesh_refinement.h"
#include "libmesh/newton_solver.h"
#include "libmesh/numeric_vector.h"
#include "libmesh/petsc_diff_solver.h"
#include "libmesh/steady_solver.h"
#include "libmesh/system_norm.h"

// Sensitivity Calculation related includes
#include "libmesh/parameter_vector.h"
#include "libmesh/sensitivity_data.h"

// Error Estimator includes
#include "libmesh/kelly_error_estimator.h"
#include "libmesh/patch_recovery_error_estimator.h"

// Adjoint Related includes
#include "libmesh/adjoint_residual_error_estimator.h"
#include "libmesh/qoi_set.h"

// libMesh I/O includes
#include "libmesh/getpot.h"
#include "libmesh/gmv_io.h"
#include "libmesh/exodusII_io.h"

// Local includes
#include "femparameters.h"
#include "L-shaped.h"
#include "L-qoi.h"

// Bring in everything from the libMesh namespace
using namespace libMesh;

// Local function declarations

// Number output files, the files are give a prefix of primal or adjoint_i depending on
// whether the output is the primal solution or the dual solution for the ith QoI

// Write gmv output
void write_output(EquationSystems & es,
                  unsigned int a_step, // The adaptive step count
                  std::string solution_type, // primal or adjoint solve
                  FEMParameters & param)
{
  // Ignore parameters when there are no output formats available.
  libmesh_ignore(es, a_step, solution_type, param);

#ifdef LIBMESH_HAVE_GMV
  if (param.output_gmv)
    {
      MeshBase & mesh = es.get_mesh();

      std::ostringstream file_name_gmv;
      file_name_gmv << solution_type
                    << ".out.gmv."
                    << std::setw(2)
                    << std::setfill('0')
                    << std::right
                    << a_step;

      GMVIO(mesh).write_equation_systems(file_name_gmv.str(), es);
    }
#endif

#ifdef LIBMESH_HAVE_EXODUS_API
  if (param.output_exodus)
    {
      MeshBase & mesh = es.get_mesh();

      // We write out one file per adaptive step. The files are named in
      // the following way:
      // foo.e
      // foo.e-s002
      // foo.e-s003
      // ...
      // so that, if you open the first one with Paraview, it actually
      // opens the entire sequence of adapted files.
      std::ostringstream file_name_exodus;

      file_name_exodus << solution_type << ".e";
      if (a_step > 0)
        file_name_exodus << "-s"
                         << std::setw(3)
                         << std::setfill('0')
                         << std::right
                         << a_step + 1;

      // We write each adaptive step as a pseudo "time" step, where the
      // time simply matches the (1-based) adaptive step we are on.
      ExodusII_IO(mesh).write_timestep(file_name_exodus.str(),
                                       es,
                                       1,
                                       /*time=*/a_step + 1);
    }
#endif
}


void adjust_linear_solver(LinearSolver<Number> & linear_solver)
{
  // Eigen's BiCGSTAB doesn't seem reliable at the full refinement
  // level we use here.
#ifdef LIBMESH_HAVE_EIGEN_SPARSE
  EigenSparseLinearSolver<Number> * eigen_linear_solver =
    dynamic_cast<EigenSparseLinearSolver<Number> *>(&linear_solver);

  if (eigen_linear_solver)
    eigen_linear_solver->set_solver_type(SPARSELU);
#else
  libmesh_ignore(linear_solver);
#endif
}

void adjust_linear_solvers(LaplaceSystem & system)
{
  auto diff_solver = cast_ptr<NewtonSolver*>(system.get_time_solver().diff_solver().get());
  if (diff_solver) // Some compilers don't like dynamic cast of nullptr?
    {
      auto solver = cast_ptr<NewtonSolver*>(diff_solver);
      if (solver)
        adjust_linear_solver(solver->get_linear_solver());
    }

  LinearSolver<Number> * linear_solver = system.get_linear_solver();
  if (linear_solver)
    adjust_linear_solver(*linear_solver);
}


// Set the parameters for the nonlinear and linear solvers to be used during the simulation
void set_system_parameters(LaplaceSystem & system, FEMParameters & param)
{
  // Use analytical jacobians?
  system.analytic_jacobians() = param.analytic_jacobians;

  // Verify analytic jacobians against numerical ones?
  system.verify_analytic_jacobians = param.verify_analytic_jacobians;

  // Use the prescribed FE type
  system.fe_family() = param.fe_family[0];
  system.fe_order() = param.fe_order[0];

  // More desperate debugging options
  system.print_solution_norms = param.print_solution_norms;
  system.print_solutions      = param.print_solutions;
  system.print_residual_norms = param.print_residual_norms;
  system.print_residuals      = param.print_residuals;
  system.print_jacobian_norms = param.print_jacobian_norms;
  system.print_jacobians      = param.print_jacobians;

  // No transient time solver
  system.time_solver = std::make_unique<SteadySolver>(system);

  // Nonlinear solver options
  if (param.use_petsc_snes)
  {
#ifdef LIBMESH_HAVE_PETSC
    system.time_solver->diff_solver() = std::make_unique<PetscDiffSolver>(system);
#else
    libmesh_error_msg("This example requires libMesh to be compiled with PETSc support.");
#endif
  }
  else
  {
    system.time_solver->diff_solver() = std::make_unique<NewtonSolver>(system);
    auto solver = cast_ptr<NewtonSolver*>(system.time_solver->diff_solver().get());

    solver->quiet                       = param.solver_quiet;
    solver->max_nonlinear_iterations    = param.max_nonlinear_iterations;
    solver->minsteplength               = param.min_step_length;
    solver->relative_step_tolerance     = param.relative_step_tolerance;
    solver->relative_residual_tolerance = param.relative_residual_tolerance;
    solver->require_residual_reduction  = param.require_residual_reduction;
    solver->linear_tolerance_multiplier = param.linear_tolerance_multiplier;
    if (system.time_solver->reduce_deltat_on_diffsolver_failure)
      {
        solver->continue_after_max_iterations = true;
        solver->continue_after_backtrack_failure = true;
      }
    system.set_constrain_in_solver(param.constrain_in_solver);

    // And the linear solver options
    solver->max_linear_iterations    = param.max_linear_iterations;
    solver->initial_linear_tolerance = param.initial_linear_tolerance;
    solver->minimum_linear_tolerance = param.minimum_linear_tolerance;
    adjust_linear_solvers(system);
  }
}

// Build the mesh refinement object and set parameters for refining/coarsening etc

#ifdef LIBMESH_ENABLE_AMR

std::unique_ptr<MeshRefinement> build_mesh_refinement(MeshBase & mesh,
                                                      FEMParameters & param)
{
  auto mesh_refinement = std::make_unique<MeshRefinement>(mesh);
  mesh_refinement->coarsen_by_parents() = true;
  mesh_refinement->absolute_global_tolerance() = param.global_tolerance;
  mesh_refinement->nelem_target()      = param.nelem_target;
  mesh_refinement->refine_fraction()   = param.refine_fraction;
  mesh_refinement->coarsen_fraction()  = param.coarsen_fraction;
  mesh_refinement->coarsen_threshold() = param.coarsen_threshold;

  return mesh_refinement;
}

#endif // LIBMESH_ENABLE_AMR

// This is where we declare the error estimators to be built and used for
// mesh refinement. The adjoint residual estimator needs two estimators.
// One for the forward component of the estimate and one for the adjoint
// weighting factor. Here we use the Patch Recovery indicator to estimate both the
// forward and adjoint weights. The H1 seminorm component of the error is used
// as dictated by the weak form the Laplace equation.

std::unique_ptr<ErrorEstimator> build_error_estimator(FEMParameters & param)
{
  if (param.indicator_type == "kelly")
    {
      libMesh::out << "Using Kelly Error Estimator" << std::endl;

      return std::make_unique<KellyErrorEstimator>();
    }
  else if (param.indicator_type == "adjoint_residual")
    {
      libMesh::out << "Using Adjoint Residual Error Estimator with Patch Recovery Weights" << std::endl << std::endl;

      auto adjoint_residual_estimator = std::make_unique<AdjointResidualErrorEstimator>();

      adjoint_residual_estimator->error_plot_suffix = "error.gmv";

      adjoint_residual_estimator->primal_error_estimator() = std::make_unique<PatchRecoveryErrorEstimator>();
      adjoint_residual_estimator->primal_error_estimator()->error_norm.set_type(0, H1_SEMINORM);

      adjoint_residual_estimator->dual_error_estimator() = std::make_unique<PatchRecoveryErrorEstimator>();
      adjoint_residual_estimator->dual_error_estimator()->error_norm.set_type(0, H1_SEMINORM);

      return adjoint_residual_estimator;
    }
  else
    libmesh_error_msg("Unknown indicator_type = " << param.indicator_type);
}

// The main program.
int main (int argc, char ** argv)
{
  // Initialize libMesh.
  LibMeshInit init (argc, argv);

  // This example requires a linear solver package.
  libmesh_example_requires(libMesh::default_solver_package() != INVALID_SOLVER_PACKAGE,
                           "--enable-petsc, --enable-trilinos, or --enable-eigen");

  // Skip adaptive examples on a non-adaptive libMesh build
#ifndef LIBMESH_ENABLE_AMR
  libmesh_example_requires(false, "--enable-amr");
#else

  libMesh::out << "Started " << argv[0] << std::endl;

  // Make sure the general input file exists, and parse it
  {
    std::ifstream i("general.in");
    libmesh_error_msg_if(!i, '[' << init.comm().rank() << "] Can't find general.in; exiting early.");
  }

  // Read in parameters from the input file
  GetPot infile("general.in");
  // But allow the command line to override it.
  infile.parse_command_line(argc, argv);

  FEMParameters param(init.comm());
  param.read(infile);

  // Skip this default-2D example if libMesh was compiled as 1D-only.
  libmesh_example_requires(2 <= LIBMESH_DIM, "2D support");

  // Create a mesh, with dimension to be overridden later, distributed
  // across the default MPI communicator.
  Mesh mesh(init.comm());

  // And an object to refine it
  std::unique_ptr<MeshRefinement> mesh_refinement =
    build_mesh_refinement(mesh, param);

  // And an EquationSystems to run on it
  EquationSystems equation_systems (mesh);

  libMesh::out << "Reading in and building the mesh" << std::endl;

  // Read in the mesh
  mesh.read(param.domainfile.c_str());
  // Make all the elements of the mesh second order so we can compute
  // with a higher order basis
  mesh.all_second_order();

  // Create a mesh refinement object to do the initial uniform refinements
  // on the coarse grid read in from lshaped.xda
  MeshRefinement initial_uniform_refinements(mesh);
  initial_uniform_refinements.uniformly_refine(param.coarserefinements);

  libMesh::out << "Building system" << std::endl;

  // Build the FEMSystem
  LaplaceSystem & system = equation_systems.add_system<LaplaceSystem> ("LaplaceSystem");

  QoISet qois;

  qois.add_indices({0});

  qois.set_weight(0, 0.5);

  // Put some scope here to test that the cloning is working right
  {
    LaplaceQoI qoi;
    system.attach_qoi(&qoi);
  }

  // Set its parameters
  set_system_parameters(system, param);

  libMesh::out << "Initializing systems" << std::endl;

  equation_systems.init ();

  // Print information about the mesh and system to the screen.
  mesh.print_info();
  equation_systems.print_info();

  {
    // Adaptively solve the timestep
    unsigned int a_step = 0;
    for (; a_step != param.max_adaptivesteps; ++a_step)
      {
        // We can't adapt to both a tolerance and a
        // target mesh size
        if (param.global_tolerance != 0.)
          libmesh_assert_equal_to (param.nelem_target, 0);
        // If we aren't adapting to a tolerance we need a
        // target mesh size
        else
          libmesh_assert_greater (param.nelem_target, 0);

        // Solve the forward problem
        system.solve();

        // Write out the computed primal solution
        write_output(equation_systems, a_step, "primal", param);

        // Get a pointer to the primal solution vector
        NumericVector<Number> & primal_solution = *system.solution;

        // A SensitivityData object to hold the qois and parameters
        SensitivityData sensitivities(qois, system, system.get_parameter_vector());

        // Make sure we get the contributions to the adjoint RHS from the sides
        system.assemble_qoi_sides = true;

        // Here we solve the adjoint problem inside the adjoint_qoi_parameter_sensitivity
        // function, so we have to set the adjoint_already_solved boolean to false
        system.set_adjoint_already_solved(false);

        // Compute the sensitivities
        system.adjoint_qoi_parameter_sensitivity(qois, system.get_parameter_vector(), sensitivities);

        // Now that we have solved the adjoint, set the adjoint_already_solved boolean to true, so we dont solve unnecessarily in the error estimator
        system.set_adjoint_already_solved(true);

        GetPot infile_l_shaped("l-shaped.in");

        Number sensitivity_QoI_0_0_computed = sensitivities[0][0];
        Number sensitivity_QoI_0_0_exact = infile_l_shaped("sensitivity_0_0", 0.0);
        Number sensitivity_QoI_0_1_computed = sensitivities[0][1];
        Number sensitivity_QoI_0_1_exact = infile_l_shaped("sensitivity_0_1", 0.0);

        libMesh::out << "Adaptive step "
                     << a_step
                     << ", we have "
                     << mesh.n_active_elem()
                     << " active elements and "
                     << equation_systems.n_active_dofs()
                     << " active dofs."
                     << std::endl;

        libMesh::out << "Sensitivity of QoI one to Parameter one is "
                     << sensitivity_QoI_0_0_computed
                     << std::endl;
        libMesh::out << "Sensitivity of QoI one to Parameter two is "
                     << sensitivity_QoI_0_1_computed
                     << std::endl;

        libMesh::out << "The relative error in sensitivity QoI_0_0 is "
                     << std::setprecision(17)
                     << std::abs(sensitivity_QoI_0_0_computed - sensitivity_QoI_0_0_exact) / std::abs(sensitivity_QoI_0_0_exact)
                     << std::endl;

        libMesh::out << "The relative error in sensitivity QoI_0_1 is "
                     << std::setprecision(17)
                     << std::abs(sensitivity_QoI_0_1_computed - sensitivity_QoI_0_1_exact) / std::abs(sensitivity_QoI_0_1_exact)
                     << std::endl
                     << std::endl;

        // Get a pointer to the solution vector of the adjoint problem for QoI 0
        NumericVector<Number> & dual_solution_0 = system.get_adjoint_solution(0);

        // Swap the primal and dual solutions so we can write out the adjoint solution
        primal_solution.swap(dual_solution_0);
        write_output(equation_systems, a_step, "adjoint_0", param);

        // Swap back
        primal_solution.swap(dual_solution_0);

        // We have to refine either based on reaching an error tolerance or
        // a number of elements target, which should be verified above
        // Otherwise we flag elements by error tolerance or nelem target

        // Uniform refinement
        if (param.refine_uniformly)
          {
            libMesh::out << "Refining Uniformly" << std::endl << std::endl;

            mesh_refinement->uniformly_refine(1);
          }
        // Adaptively refine based on reaching an error tolerance
        else if (param.global_tolerance >= 0. && param.nelem_target == 0.)
          {
            // Now we construct the data structures for the mesh refinement process
            ErrorVector error;

            // Build an error estimator object
            std::unique_ptr<ErrorEstimator> error_estimator =
              build_error_estimator(param);

            // Estimate the error in each element using the Adjoint Residual or Kelly error estimator
            error_estimator->estimate_error(system, error);

            mesh_refinement->flag_elements_by_error_tolerance (error);

            mesh_refinement->refine_and_coarsen_elements();
          }
        // Adaptively refine based on reaching a target number of elements
        else
          {
            // Now we construct the data structures for the mesh refinement process
            ErrorVector error;

            // Build an error estimator object
            std::unique_ptr<ErrorEstimator> error_estimator =
              build_error_estimator(param);

            // Estimate the error in each element using the Adjoint Residual or Kelly error estimator
            error_estimator->estimate_error(system, error);

            if (mesh.n_active_elem() >= param.nelem_target)
              {
                libMesh::out<<"We reached the target number of elements."<<std::endl <<std::endl;
                break;
              }

            mesh_refinement->flag_elements_by_nelem_target (error);

            mesh_refinement->refine_and_coarsen_elements();
          }

        // Dont forget to reinit the system after each adaptive refinement !
        equation_systems.reinit();

        // Fix up the linear solver options if that reinit just cleared it
        adjust_linear_solvers(system);

        libMesh::out << "Refined mesh to "
                     << mesh.n_active_elem()
                     << " active elements and "
                     << equation_systems.n_active_dofs()
                     << " active dofs."
                     << std::endl;
      }

    // Do one last solve if necessary
    if (a_step == param.max_adaptivesteps)
      {
        system.solve();

        write_output(equation_systems, a_step, "primal", param);

        system.assemble_qoi_sides = true;

        SensitivityData sensitivities(qois, system, system.get_parameter_vector());

        // Here we solve the adjoint problem inside the adjoint_qoi_parameter_sensitivity
        // function, so we have to set the adjoint_already_solved boolean to false
        system.set_adjoint_already_solved(false);

        system.adjoint_qoi_parameter_sensitivity(qois, system.get_parameter_vector(), sensitivities);

        // Now that we have solved the adjoint, set the adjoint_already_solved boolean to true, so we dont solve unnecessarily in the error estimator
        system.set_adjoint_already_solved(true);

        GetPot infile_l_shaped("l-shaped.in");

        Number sensitivity_QoI_0_0_computed = sensitivities[0][0];
        Number sensitivity_QoI_0_0_exact = infile_l_shaped("sensitivity_0_0", 0.0);
        Number sensitivity_QoI_0_1_computed = sensitivities[0][1];
        Number sensitivity_QoI_0_1_exact = infile_l_shaped("sensitivity_0_1", 0.0);

        libMesh::out << "Adaptive step "
                     << a_step
                     << ", we have "
                     << mesh.n_active_elem()
                     << " active elements and "
                     << equation_systems.n_active_dofs()
                     << " active dofs."
                     << std::endl;

        libMesh::out << "Sensitivity of QoI one to Parameter one is "
                     << sensitivity_QoI_0_0_computed
                     << std::endl;

        libMesh::out << "Sensitivity of QoI one to Parameter two is "
                     << sensitivity_QoI_0_1_computed
                     << std::endl;

        libMesh::out << "The error in sensitivity QoI_0_0 is "
                     << std::setprecision(17)
                     << std::abs(sensitivity_QoI_0_0_computed - sensitivity_QoI_0_0_exact)/sensitivity_QoI_0_0_exact
                     << std::endl;

        libMesh::out << "The error in sensitivity QoI_0_1 is "
                     << std::setprecision(17)
                     << std::abs(sensitivity_QoI_0_1_computed - sensitivity_QoI_0_1_exact)/sensitivity_QoI_0_1_exact
                     << std::endl
                     << std::endl;

        // Hard coded asserts to ensure that the actual numbers we are getting are what they should be
        libmesh_assert_less(std::abs((sensitivity_QoI_0_0_computed - sensitivity_QoI_0_0_exact)/sensitivity_QoI_0_0_exact), 2.e-4);
        libmesh_assert_less(std::abs((sensitivity_QoI_0_1_computed - sensitivity_QoI_0_1_exact)/sensitivity_QoI_0_1_exact), 2.e-4);

        // Let's do a forward sensitivity solve too, unless we're
        // told to skip it for backwards compatibility with old
        // performance benchmarks.
        const bool forward_sensitivity = infile("--forward_sensitivity", true);

        // Don't confuse PETSc with our custom GetPot's arguments
        libMesh::add_command_line_names(infile);

        if (forward_sensitivity)
          {
            // This will require two linear solves (one per parameter)
            // rather than the adjoint sensitivity's one, but it's useful
            // for regression testing.
            SensitivityData forward_sensitivities(qois, system, system.get_parameter_vector());
            system.forward_qoi_parameter_sensitivity(qois, system.get_parameter_vector(), forward_sensitivities);

            libmesh_assert_less(std::abs((forward_sensitivities[0][0] - sensitivity_QoI_0_0_exact)/sensitivity_QoI_0_0_exact), 2.e-4);
            libmesh_assert_less(std::abs((forward_sensitivities[0][1] - sensitivity_QoI_0_1_exact)/sensitivity_QoI_0_1_exact), 2.e-4);

            // These should be the same linearization, just calculated
            // different ways with different roundoff error
            libmesh_assert_less
              (std::abs((forward_sensitivities[0][0] - sensitivity_QoI_0_0_computed)/sensitivity_QoI_0_0_computed), TOLERANCE);
            libmesh_assert_less
              (std::abs((forward_sensitivities[0][1] - sensitivity_QoI_0_1_computed)/sensitivity_QoI_0_1_computed), TOLERANCE);

            libMesh::out << "The error in forward calculation of sensitivity QoI_0_0 is "
                         << std::setprecision(17)
                         << std::abs(forward_sensitivities[0][0] - sensitivity_QoI_0_0_exact)/sensitivity_QoI_0_0_exact
                         << std::endl;

            libMesh::out << "The error in forward calculation of sensitivity QoI_0_1 is "
                         << std::setprecision(17)
                         << std::abs(forward_sensitivities[0][1] - sensitivity_QoI_0_1_exact)/sensitivity_QoI_0_1_exact
                         << std::endl
                         << std::endl;


          }

        NumericVector<Number> & primal_solution = *system.solution;
        NumericVector<Number> & dual_solution_0 = system.get_adjoint_solution(0);
        primal_solution.swap(dual_solution_0);
        write_output(equation_systems, a_step, "adjoint_0", param);

        primal_solution.swap(dual_solution_0);
      }
  }

  libMesh::err << '[' << system.processor_id()
               << "] Completing output."
               << std::endl;

#endif // #ifndef LIBMESH_ENABLE_AMR

  // All done.
  return 0;
}
