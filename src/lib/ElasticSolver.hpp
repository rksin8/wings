#pragma once

#include <deal.II/fe/fe_system.h>
#include <deal.II/numerics/vector_tools.h>  // interpolate_boundary_values

// custom modules
#include <Math.hpp>


namespace SolidSolvers
{
using namespace dealii;


template <int dim>
class ElasticSolver
{
 public:
  ElasticSolver(MPI_Comm                                  &mpi_communicator,
                    parallel::distributed::Triangulation<dim> &triangulation,
                    const Model::Model<dim>                   &model,
                    ConditionalOStream                        &pcout);
  ~ElasticSolver();
  /* setup degrees of freedom for the current triangulation
   * and allocate memory for solution vectors */
  void setup_dofs();
  // Fill system matrix and rhs vector
  void assemble_system(const TrilinosWrappers::MPI::Vector & pressure_vector);
  // solve linear system syste_matrix*solution= rhs_vector
  unsigned int solve();
  // give solver access to fluid dofs
  void set_coupling(const DoFHandler<dim> &fluid_dof_handler);
  // accessing private members
  const TrilinosWrappers::SparseMatrix & get_system_matrix();
  const TrilinosWrappers::MPI::Vector  & get_rhs_vector();
  const DoFHandler<dim>                & get_dof_handler();
  const FESystem<dim>                  & get_fe();

 private:
  MPI_Comm                                  & mpi_communicator;
  parallel::distributed::Triangulation<dim> & triangulation;
  DoFHandler<dim>                             dof_handler;
  FESystem<dim>                               fe;
  const Model::Model<dim>                   & model;
  ConditionalOStream                        & pcout;
  // Matrices and vectors
  TrilinosWrappers::SparseMatrix         system_matrix;
  TrilinosWrappers::MPI::Vector          rhs_vector;
  const DoFHandler<dim>                * p_fluid_dof_handler;
  ConstraintMatrix                       constraints;

 public:
  // solution vectors
  TrilinosWrappers::MPI::Vector solution, old_solution;
  TrilinosWrappers::MPI::Vector relevant_solution;
  // partitioning
  IndexSet                      locally_owned_dofs, locally_relevant_dofs;
};



template <int dim>
ElasticSolver<dim>::
ElasticSolver(MPI_Comm                                  &mpi_communicator,
              parallel::distributed::Triangulation<dim> &triangulation,
              const Model::Model<dim>                   &model,
              ConditionalOStream                        &pcout)
    :
    mpi_communicator(mpi_communicator),
    triangulation(triangulation),
    dof_handler(triangulation),
    fe(FE_Q<dim>(1), dim), // dim linear shape functions
    model(model),
    pcout(pcout)
{}



template <int dim>
ElasticSolver<dim>::~ElasticSolver()
{
  dof_handler.clear();
} // eom



template <int dim>
void
ElasticSolver<dim>::set_coupling(const DoFHandler<dim> &fluid_dof_handler)
{
  p_fluid_dof_handler = &fluid_dof_handler;
} // eom



template <int dim>
void
ElasticSolver<dim>::setup_dofs()
{
  dof_handler.distribute_dofs(fe);

  { // partitioning
    locally_owned_dofs.clear();
    locally_relevant_dofs.clear();
    locally_owned_dofs = dof_handler.locally_owned_dofs();
    DoFTools::extract_locally_relevant_dofs(dof_handler,
                                            locally_relevant_dofs);
  }
  { // constraints
    constraints.clear();
    DoFTools::make_hanging_node_constraints(dof_handler, constraints);

    // add dirichlet BC's to constraints
    std::vector<ComponentMask> mask(dim);
    for (unsigned int comp=0; comp<dim; ++comp)
    {
      FEValuesExtractors::Scalar extractor(comp);
      mask[comp] = fe.component_mask(extractor);
    }
    int n_dirichlet_conditions = model.solid_dirichlet_labels.size();

    for (int cond=0; cond<n_dirichlet_conditions; ++cond)
    {
      int component = model.solid_dirichlet_components[cond];
      double dirichlet_value = model.solid_dirichlet_values[cond];
      VectorTools::interpolate_boundary_values
          (dof_handler,
           model.solid_dirichlet_labels[cond],
           ConstantFunction<dim>(dirichlet_value, dim),
           constraints,
           mask[component]);
    }

    constraints.close();
  }
  { // system matrix
    system_matrix.clear();
    TrilinosWrappers::SparsityPattern
        sparsity_pattern(locally_owned_dofs, mpi_communicator);
    DoFTools::make_sparsity_pattern(dof_handler, sparsity_pattern,
                                    constraints,
                                    /* keep_constrained_dofs =  */ false);
    sparsity_pattern.compress();
    system_matrix.reinit(sparsity_pattern);
  }
  { // vectors
    solution.reinit(locally_owned_dofs, mpi_communicator);
    relevant_solution.reinit(locally_relevant_dofs, mpi_communicator);
    old_solution.reinit(locally_relevant_dofs, mpi_communicator);
    rhs_vector.reinit(locally_owned_dofs, locally_relevant_dofs,
                      mpi_communicator, /* omit-zeros=*/ true);
  }

}  // eom



template <int dim>
void
ElasticSolver<dim>::
assemble_system(const TrilinosWrappers::MPI::Vector & pressure_vector)
{
  const auto &  fluid_fe = p_fluid_dof_handler->get_fe();

  QGauss<dim>   fvm_quadrature_formula(1);
  QGauss<dim>   quadrature_formula(fe.degree + 1);

  FEValues<dim> fe_values(fe, quadrature_formula,
                          update_values | update_gradients |
                          update_JxW_values);
  FEValues<dim> fluid_fe_values(fluid_fe,
                                fvm_quadrature_formula,
                                update_values);

  // we need this because FeSystem class is weird
  // we use this extractor to extract all (displacement) dofs
  const FEValuesExtractors::Vector displacement(0);

  const unsigned int dofs_per_cell = fe.dofs_per_cell;
  const unsigned int n_q_points = quadrature_formula.size();

  std::vector<types::global_dof_index> local_dof_indices(dofs_per_cell);
  FullMatrix<double>                   cell_matrix(dofs_per_cell, dofs_per_cell);
  Vector<double>                       cell_rhs(dofs_per_cell);
  std::vector< Tensor<2,dim> > eps_u(dofs_per_cell);
  std::vector< Tensor<2,dim> > sigma_u(dofs_per_cell);
  std::vector< Tensor<2,dim> > grad_xi_u(dofs_per_cell);
  std::vector<double> 				 p_values(1); // 1 point since FVM
  Tensor<2,dim> identity_tensor = Math::get_identity_tensor<dim>();


  std::vector<types::global_dof_index> dof_indices(dofs_per_cell);

  typename DoFHandler<dim>::active_cell_iterator
      cell = dof_handler.begin_active(),
      endc = dof_handler.end(),
      fluid_cell = p_fluid_dof_handler->begin_active();

  system_matrix = 0;
  rhs_vector = 0;

  for (; cell!=endc; ++cell, ++fluid_cell)
    if (cell->is_locally_owned())
    {
      fe_values.reinit(cell);
      fluid_fe_values.reinit(fluid_cell);

      cell_matrix = 0;
      cell_rhs = 0;

      fluid_fe_values.get_function_values(pressure_vector, p_values);
      const double p_value = p_values[0];

			const double E = model.get_young_modulus->value(cell->center(), 0);
			const double nu = model.get_poisson_ratio->value(cell->center(), 0);
			const double lame_constant = E*nu/((1.+nu)*(1.-2*nu));
			const double shear_modulus = 0.5*E/(1.+nu);
      const double alpha = model.get_biot_coefficient();

      for (unsigned int q=0; q<n_q_points; ++q)
      {
        // compute stresses and strains for each local dof
        for (unsigned int k=0; k<dofs_per_cell; ++k)
        {
					grad_xi_u[k]   = fe_values[displacement].gradient(k, q);
					eps_u[k] 			 = 0.5*(grad_xi_u[k] + transpose(grad_xi_u[k]));
          sigma_u[k] =
              lame_constant*trace(eps_u[k])*identity_tensor +
              2*shear_modulus*eps_u[k];
        } // end k loop

        for (unsigned int i=0; i<dofs_per_cell; ++i)
        {
          for (unsigned int j=0; j<dofs_per_cell; ++j)
          {
            cell_matrix(i, j ) +=
                scalar_product(sigma_u[j], eps_u[i]) *
                fe_values.JxW(q);
          } // end j loop

          cell_rhs(i)  +=
              alpha*p_value*trace(grad_xi_u[i])*fe_values.JxW(q);
        }  // end i loop
      }  // end q loop

      cell->get_dof_indices(local_dof_indices);
      constraints.distribute_local_to_global
          (cell_matrix, cell_rhs, local_dof_indices,
           system_matrix, rhs_vector);
    } // end cell loop

  system_matrix.compress(VectorOperation::add);
  rhs_vector.compress(VectorOperation::add);
}  // eom


} // end of namespace
