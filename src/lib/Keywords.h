#pragma once

#include <string>
/*
  This class holds the strings that designate the names
  of sections and parameters in the input file
 */
namespace Keywords
{
  /* class Keywords */
  /* { */
  /* public: */
  const std::string
  section_mesh = "Mesh",
    mesh_file = "Mesh file",
    global_refinement_steps = "Global refinement steps",
    adaptive_refinement_steps = "Adaptive refinement steps",
    local_refinement_regions = "Local refinement regions",

  section_wells = "Well data" ,
    well_parameters = "Wells",
    well_schedule = "Schedule",

  section_equation_data = "Equation data" ,
    model_type = "Model",
    model_single_liquid = "SingleLiquid",
    model_single_gas = "SingleGas",
    model_water_oil = "WaterOil",
    model_water_gas = "WaterGas",
    model_blackoil = "Blackoil",
    model_single_liquid_elasticity = "SingleLiquidElasticity",
    model_single_gas_elasticity = "SingleGasElasticity",
    model_water_oil_elasticity = "WaterOilElasticity",
    model_water_gas_elasticity = "WaterGasElasticity",
    model_blackoil_elasticity = "BlackoilElasticity",
    unit_system = "Units",
    si_units = "Metric",
    field_units = "Field",
    young_modulus = "Young modulus",
    poisson_ratio = "Poisson ratio",
    volume_factor_water = "Volume factor water",
    // volume_factor_oil = "Volume factor oil",
    // volume_factor_gas = "Volume factor gas",
    viscosity_water = "Viscosity water",
    // viscosity_oil = "Viscosity oil",
    // viscosity_gas = "Viscosity gas",
    density_sc_water = "Density water",
    density_sc_oil = "Density oil",
    density_sc_gas = "Density gas",
    compressibility_water = "Compressibility water",
    pvt_oil = "PVT oil",
    pvt_water = "PVT water",
    rel_perm_water = "Rel perm water",
    rel_perm_oil = "Rel perm oil",
    permeability = "Permeability",
    permeability_anisotropy = "Perm anisotropy",
    porosity = "Porosity",

  section_solver = "Solver",
    t_max = "T max",
    time_stepping = "Time stepping",
    minimum_time_step = "Minimum time step",
    fss_tolerance = "FSS tolerance",
    max_fss_steps = "Max FSS steps";

  // Output names
  const std::string
      vtu_dir_name = "vtu",
      vtu_file_prefix = "solution-",
      vtu_file_suffix = "vtu",
      pvtu_file_prefix = "solution-",
      pvtu_file_suffix = "pvtu",
      pvd_file_name = "solution.pvd";

  /* }; */
}
