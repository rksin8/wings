#pragma once

#include <deal.II/base/function.h>
#include <deal.II/base/point.h>
#include <deal.II/dofs/dof_handler.h>
#include <deal.II/fe/fe_dgq.h>
#include <deal.II/fe/fe_values.h>
#include <deal.II/base/quadrature_lib.h>
#include <math.h>

#include <DefaultValues.cc>
#include <Schedule.cc>
#include <Math.hpp>


namespace Wellbore
{
	using namespace dealii;

  template <int dim>
  using CellIterator = typename DoFHandler<dim>::active_cell_iterator;

	template <int dim>
	class Wellbore : public Function<dim>
  {
  public:
    Wellbore(const std::vector< Point<dim> >& locations_,
             const double                     radius_);
    void set_control(const Schedule::WellControl& control_);
    const Schedule::WellControl &get_control();
    double get_radius() const;

    void locate(const DoFHandler<dim>& dof_handler,
                const FE_DGQ<dim>&     fe);
    const  std::vector<CellIterator<dim>> & get_cells();
    const  std::vector< Point<dim> >      & get_locations();
    std::pair<double,double> get_J_and_Q(const CellIterator<dim> & cell) const;
    double get_transmissibility(const CellIterator<dim> & cell) const;
    void update_productivity(const Function<dim>* permeability);
    std::vector<double> & get_productivities();
    static bool point_inside_cell(const CellIterator<dim> &cell,
                                  const Point<dim>        &p);


  private:
    double compute_productivity(const double k1, const double k2,
                                    const double dx1, const double dx2,
                                    const double length) const;
    double get_segment_length(const Point<dim>                       &start,
                              const CellIterator<dim>                &cell,
                              const Tensor<1,dim>                    &tangent,
                              const std::pair<Point<dim>,Point<dim>> &end_points);
    std::vector< Tensor<1,dim> >
    get_cell_sizes(const std::vector<CellIterator<dim>> &cells_) const;

    void get_cell_size(FEFaceValues<dim> &fe_face_values,
                       const CellIterator<dim> &cell,
                       Tensor<1,dim> &h) const;

    int    find_cell(const CellIterator<dim> & cell) const;
    bool neighbor_is_farther(const Tensor<1,dim> &cell_to_wellbore,
                              const Tensor<1,dim> &neighbor_to_wellbore,
                              const unsigned int  cell_index,
                              const unsigned int  neighbor_index,
                              const double        tolerance) const;
    bool aligned_with_face(const Tensor<1,dim> &cell_to_wellbore,
                           const Tensor<1,dim> &face_normal) const;

    // variables
    std::vector< Point<dim> > locations;
    double                    radius;
    Schedule::WellControl     control;

  	const DoFHandler<dim>          *p_dof_handler;
    std::vector<CellIterator<dim>> cells;
    std::vector<double>            segment_length;
    std::vector< Tensor<1,dim> >   segment_direction;
    std::vector<double>            productivities;
  };  // eom


  template <int dim>
  Wellbore<dim>::Wellbore(const std::vector< Point<dim> >& locations_,
                          const double                     radius_)
    :
    locations(locations_),
    radius(radius_)
  {
    AssertThrow(locations.size() > 0,
                ExcMessage("That ain't no a proper well"));
    AssertThrow(radius > 0,
                ExcMessage("Well radius should be a positive number"));
    // check for duplicates
    for (unsigned int i=1; i<locations.size(); i++)
      AssertThrow((locations[i] - locations[i-1]).norm() > 0,
                  ExcMessage("Duplicates in wellbore locations"));
    // Init zero rate control just in case
    control.type = Schedule::WellControlType::flow_control_total;
    control.value = 0.0;

  } //  eom


  template <int dim>
  inline
  void Wellbore<dim>::set_control(const Schedule::WellControl& control_)
  {
    control = control_;
  }  // eom


  template <int dim>
  inline
  const Schedule::WellControl &
  Wellbore<dim>::get_control()
  {
    return this->control;
  }  // eom

  template <int dim>
  inline
  double
  Wellbore<dim>::get_radius() const
  {
    return radius;
  }  // eom


  template <int dim>
  inline
  const std::vector<CellIterator<dim>> &
  Wellbore<dim>::get_cells()
  {
    return cells;
  }  // eom


  template <int dim>
  inline bool Wellbore<dim>::
  neighbor_is_farther(const Tensor<1,dim> &cell_to_wellbore,
                      const Tensor<1,dim> &neighbor_to_wellbore,
                      const unsigned int  cell_index,
                      const unsigned int  neighbor_index,
                      const double        tolerance) const
  {
    const bool well_closer_to_cell =
        cell_to_wellbore.norm() <= neighbor_to_wellbore.norm() + tolerance;
    const bool neighbor_closer_to_cell =
        cell_to_wellbore.norm() + tolerance >= neighbor_to_wellbore.norm();
    if (well_closer_to_cell && !neighbor_closer_to_cell)
      return true;
    else if (well_closer_to_cell && neighbor_closer_to_cell)
    {
      if (neighbor_index > cell_index)
        return true;
      else
        return false;
    }
    else
      return false;
  }  // eom

  template <int dim>
  inline bool Wellbore<dim>::
  aligned_with_face(const Tensor<1,dim> &cell_to_wellbore,
                    const Tensor<1,dim> &face_normal) const
  {
    const Tensor<1,dim> n = Math::normalize(cell_to_wellbore);
    const bool result =
        abs(scalar_product(n, face_normal))/n.norm() >
        cos(DefaultValues::small_angle);

    return result;
  }  // eom


  template <int dim>
  void Wellbore<dim>::locate(const DoFHandler<dim>& dof_handler,
                             const FE_DGQ<dim>&     fe)
  {
    /* Algorithm:
       I. if just one well location, add cell that contains the point.
       And break. so no other cells can claim the well

       II. If well segments.
       let the segment be defined with eq x = x0 + at,
       where x is a point on the segment, x0 is the beginning of the segment,
       a is the vector in the direction of the segment, t is a scalar.
       the cell center location is p0.
       x1 is the end point of the segment.

       point d is the closest point to p0 on the segment.
       vector n is between p0 and d.

       We make three checks:
       0. If cell already added from another well segment -> discard cell
       1. Calculate vectors n and d. If d is not in the cell -> discard segment.
       2. if d is in the cell but lies outside the segment
       and segment end-points are outside too -> discard.
       3. We check whether the wellbore is aligned with the cell face, and if
       yes, assign it to only one cell.
       4. if only touches a cell in a vertex that's also bad

       we also calculate dl - the length of the well segment in each cell
     */
    p_dof_handler = &dof_handler;

    Point<dim> x0, x1, p0, p1, d, start;
    Tensor<1,dim> a, n, nf;

    // we need fe_face_values to get cell normals
    QGauss<dim-1>     face_quadrature_formula(1);
    FEFaceValues<dim> fe_face_values(fe, face_quadrature_formula,
                                     update_normal_vectors);
    FESubfaceValues<dim> fe_subface_values(fe, face_quadrature_formula,
                                           update_normal_vectors);

    cells.clear();

    typename DoFHandler<dim>::active_cell_iterator
		  cell = dof_handler.begin_active(),
      // neighbor_cell = dof_handler.begin_active(),
		  endc = dof_handler.end();

	  for (; cell!=endc; ++cell)
    // if (cell->is_locally_owned)
    {
      p0 = cell->center();
      // std::cout << "\nCell " << p0 << std::endl;

      auto it = std::find(cells.begin(), cells.end(), cell);
      if (it != cells.end())
        continue;

      if(locations.size() == 1)  // case wellbore is one point
      {
        // std::cout << "Case one point" << std::endl;
        if (cell->point_inside(locations[0]))
        {
          // std::cout << "Point inside" << std::endl;

          cells.push_back(cell);
          // single-point wells are vertical
          Tensor<1,3> direction;
          direction.clear();
          direction[2] = 1;
          segment_direction.push_back(direction);
          std::vector<CellIterator<dim>> cell_container(1);
          cell_container[0] = cell;
          const std::vector<Tensor<1,dim>> h =
            get_cell_sizes(cell_container);
          segment_length.push_back(h[0][2]);

          // cell = endc;
          break;
        }  // end if point inside

      }  // end case I

      else  // well segments
        for (unsigned int i=1; i<locations.size(); i++)
        {
          // std::cout << "\nsegment i = " << i << std::endl;
          // std::cout << "\nCell " << p0 << std::endl;

          x0 = locations[i-1];
          x1 = locations[i];
          a = x1 - x0;
          const double segment_len = a.norm();
          a = a/segment_len;
          // distance from cell center to the line
          d = x0 + a*scalar_product(p0-x0, a);
          n = d - p0;
          // if (n.norm != 0.0)
          //   n /= n.norm();

          // std::cout << "a " << a << std::endl;
          // std::cout << "d " << d << std::endl;
          // std::cout << "n " << n << std::endl;
          // // check d inside cell
          // if (!(
          //         cell->point_inside(d) ||
          //         cell->point_inside(d + Point<dim>(eps,0,0))  ||
          //         cell->point_inside(d + Point<dim>(-eps,0,0)) ||
          //         cell->point_inside(d + Point<dim>(0,eps,0))  ||
          //         cell->point_inside(d + Point<dim>(0,-eps,0)) ||
          //         cell->point_inside(d + Point<dim>(0,0,eps))  ||
          //         cell->point_inside(d + Point<dim>(0,0,-eps))
          //       ))
          if (!point_inside_cell(cell, d))
          {
            // std::cout << "No" << std::endl;
            continue;
          }
          // check if d is between x1 and x0
          // d = x0 + a*td
          double td = scalar_product((p0 - x0), a);
          // x1 = x0 + a*t1
          double t1 = segment_len;

          // std::cout << "td = " << td << std::endl;

          const bool x0_inside = cell->point_inside(x0);
          const bool x1_inside = cell->point_inside(x1);

          if((td < 0 || td > t1) && // distance vector outside segment
             !(x0_inside || x1_inside)) //end-points
          {
            // std::cout << "No" << std::endl;
            continue;
          }
          // std::cout << "Yes" << std::endl;

          // initial point to seek segment length
          if (td < 0 && x0_inside)
            start = x0;
          if (td > t1 && x1_inside)
            start = x1;
          if (td >= 0 && td <= t1)
            start = d;

          // check if segment aligned with faces and select the closest cell
          // tolerance
          const double eps = DefaultValues::small_number*cell->diameter();
          bool skip_cell = false;
          // std::cout << "testing cell " << cell->center() << " for alignment" << std::endl;
          for (unsigned int f=0; f<GeometryInfo<dim>::faces_per_cell; ++f)
            if (!cell->at_boundary(f))
            {
              // std::cout << "Testing face " << f << std::endl;
              if((cell->neighbor(f)->level() == cell->level() &&
                 cell->neighbor(f)->has_children() == false) ||
                 cell->neighbor_is_coarser(f))
              {
                fe_face_values.reinit(cell, f);
                nf = fe_face_values.normal_vector(0); // 0 is gauss point
                p1 = cell->neighbor(f)->center();
                const bool face_aligned_with_well =
                    aligned_with_face(n, nf);
                const bool well_closer_to_cell =
                    neighbor_is_farther(n, p1-d, cell->active_cell_index(),
                                        cell->neighbor(f)->active_cell_index(),
                                        eps);
                if (face_aligned_with_well && !well_closer_to_cell)
                {
                  skip_cell = true;
                  break;
                }
              } // end same level case or neighbor is coarser case
              else if ((cell->neighbor(f)->level() == cell->level()) &&
                       (cell->neighbor(f)->has_children() == true))
                for (unsigned int subface=0;
                     subface<cell->face(f)->n_children(); ++subface)
                {
                  fe_subface_values.reinit(cell, f, subface);
                  nf = fe_subface_values.normal_vector(0); // 0 is gauss point
                  const auto & neighbor_child
                      = cell->neighbor_child_on_subface(f, subface);
                  p1 = neighbor_child->center();
                  const bool face_aligned_with_well =
                      aligned_with_face(n, nf);
                  const bool well_closer_to_cell =
                      neighbor_is_farther(n, p1-d, cell->active_cell_index(),
                                          neighbor_child->active_cell_index(),
                                          eps);
                  if (face_aligned_with_well && !well_closer_to_cell)
                  {
                    skip_cell = true;
                  }
                } // end neighbor has children

            } // end face loop

          if (skip_cell)
          {
            // std::cout << "OK but another cell is better" << std::endl;
            continue;
          }


          const double l = get_segment_length(start, cell, a,
                                              std::make_pair(x0, x1));
          // std::cout << "\nCell " << p0 << std::endl;
          // std::cout << "segment  = " << i << std::endl;
          // std::cout << "Segment length = " << l << std::endl;
          // if no other segment contains the cell
          const int cell_exists = find_cell(cell);
          if (cell_exists == -1)
          {
            cells.push_back(cell);
            segment_length.push_back(l);
            segment_direction.push_back(a);
          }
          else
          {
            segment_length[cell_exists] += l;
            // take average of the tangents
            const auto old_a = segment_direction[cell_exists];
            segment_direction[l] = 0.5*(old_a + a);
          }
        } // end loop segments

    }  // end cell loop
  }  // eom

  template <int dim>
  bool Wellbore<dim>::
  point_inside_cell(const CellIterator<dim> &cell,
                    const Point<dim>        &p)
  {
    // const double eps = DefaultValues::small_number*cell->diameter();
    const double eps = DefaultValues::small_number_geometry*cell->diameter();
    if (
          cell->point_inside(p + Point<dim>(eps,0,0))  ||
          cell->point_inside(p + Point<dim>(-eps,0,0)) ||
          cell->point_inside(p + Point<dim>(0,eps,0))  ||
          cell->point_inside(p + Point<dim>(0,-eps,0)) ||
          cell->point_inside(p + Point<dim>(0,0,eps))  ||
          cell->point_inside(p + Point<dim>(0,0,-eps))
        )
      return true;
    else
      return false;
  }  // eom


  template <int dim>
  double Wellbore<dim>::
  get_segment_length(const Point<dim>                       &start,
                     const CellIterator<dim>                &cell,
                     const Tensor<1,dim>                    &tangent,
                     const std::pair<Point<dim>,Point<dim>> &end_points)
  {
    /* Assuming that the start point is in the cell,
       calculate the length of the well segment in the cell */

    // first check if the tangent is a unit vector
    Tensor<1,dim> t = tangent;
    if (abs(t.norm() - 1.0) > DefaultValues::small_number)
      t = t/t.norm();

    const double d = (end_points.second - end_points.first).norm();
    const double step = d*DefaultValues::small_number_geometry;

    double length = 0;
    // first move in the direction of a
    Point<dim> p = start, pp = start;
    while (point_inside_cell(cell, p))
    {
      // check if point still between endpoints
      if (start.distance(p) > start.distance(end_points.second))
        break;
      length += (p - pp).norm();
      // std::cout << "current_length = " << length << std::endl;
      pp = p;
      p = p + t*step;
    } // end moving along tangent
    // std::cout << "moving back " << std::endl;
    // then move in the opposite direction
    p = start, pp = start;
    while (point_inside_cell(cell, p))
    {
      if (start.distance(p) > start.distance(end_points.first))
        break;
      length += (p - pp).norm();
      // std::cout << "current_length = " << length << std::endl;
      pp = p;
      p = p - t*step;
    } // end moving along tangent

    return length;
  }  // eom

  template <int dim>
  const std::vector< Point<dim> > &
  Wellbore<dim>::get_locations()
  {
    return locations;
  } // eom


  template <int dim>
  int Wellbore<dim>::find_cell(const CellIterator<dim> & cell) const
  {
    /*
      Returns index in this->wells, segment_length, segment_direction
      if not found returns 1
     */
    for (unsigned int i=0; i<cells.size(); i++)
    {
      if (cell == cells[i])
        return i;
      else
        continue;
    }

    return -1;
  }  // eom


  template <int dim>
  void
  Wellbore<dim>::get_cell_size(FEFaceValues<dim> &fe_face_values,
                               const CellIterator<dim> &cell,
                               Tensor<1,dim> &h) const
  { // compute size of one cell into h
    // first fill min_max otherwise may get weird values
    std::vector<std::pair <double,double> > min_max(dim);
    for (int d=0; d<dim; d++)
    {
      min_max[d].first = cell->center()[d];
      min_max[d].second = cell->center()[d];
    }
    // loop over faces and figure out dx dy dz
    for (unsigned int f=0; f<GeometryInfo<dim>::faces_per_cell; ++f)
    {
      fe_face_values.reinit(cell, f);
      const Point<dim> q_point = fe_face_values.quadrature_point(0);
      for (int d=0; d<dim; d++)
      {
        if (q_point[d] < min_max[d].first)
          min_max[d].first = q_point[d];
        if (q_point[d] > min_max[d].second)
          min_max[d].second = q_point[d];
      }
    }  // end face loop

    for (int d=0; d<dim; d++)
      h[d] = min_max[d].second - min_max[d].first;
  }  // eom


  template <int dim>
  std::vector< Tensor<1,dim> >
  Wellbore<dim>::
  get_cell_sizes(const std::vector<CellIterator<dim>> &cells_) const
  { // compute cell sizes for the cells in the cells_ vector
    /*
      Loop cells, loop faces, find minimum and maximum coordinate
      for each cell, take differences, those are dx dy dz
    */
    const auto & dof_handler = (*p_dof_handler);
    const auto & fe = dof_handler.get_fe();
    QGauss<dim-1>     face_quadrature_formula(1);
    FEFaceValues<dim> fe_face_values(fe, face_quadrature_formula,
                                     update_quadrature_points);
    // We can't do dim here, because we need all three dimensions
    // for the productivity calculation
    std::vector< Tensor<1,dim> > h(cells.size()); //
    int counter = 0;

    for (const auto & cell : cells_)
    {
      get_cell_size(fe_face_values, cell, h[counter]);
      counter++;
    }  // end cell loop

    return h;
  }  // eom


  template <int dim>
  std::vector<double> &
  Wellbore<dim>:: get_productivities()
  {
    return productivities;
  } // eom


  template <int dim>
  void Wellbore<dim>::
  update_productivity(const Function<dim>* get_permeability)
  {
    /*
      First get cell dimensions dx dy dz
      First compute the sum of permeabilities for the flux normalization
      Then compute transmissibilities.
      How do I normalize permeability when it's a tensor?
     */
    Vector<double>    perm(dim);
    Tensor<1,dim>     productivity;
    const std::vector< Tensor<1,dim> > h = get_cell_sizes(cells);
    for (unsigned int i=0; i<cells.size(); i++)
    {
      get_permeability->vector_value(cells[i]->center(), perm);
      productivity[0] = compute_productivity
        (perm[1], perm[2], h[i][1], h[i][2],
         segment_length[i]*abs(segment_direction[i][0]));
      productivity[1] = compute_productivity
        (perm[0], perm[2], h[i][0], h[i][2],
         segment_length[i]*abs(segment_direction[i][1]));
      productivity[2] = compute_productivity
        (perm[0], perm[1], h[i][0], h[i][1],
         segment_length[i]*abs(segment_direction[i][2]));
      productivities.push_back(productivity.norm());
      // std::cout << "J: " << productivity << std::endl;
      // const auto & cell =
    }  // end cell loop
  }  // eom


  template <int dim>
  double Wellbore<dim>::compute_productivity(const double k1,
                                             const double k2,
                                             const double dx1,
                                             const double dx2,
                                             const double length) const
  {
    // pieceman radius
    const double r =
      0.28*std::sqrt(std::sqrt(k2/k1)*dx1*dx1 + std::sqrt(k1/k2)*dx2*dx2) /
      (std::pow(k2/k1, 0.25) + std::pow(k1/k2, 0.25));
    double trans =
      2*M_PI*std::sqrt(k1*k2)*length/(std::log(r/radius) + control.skin);
    // std::cout << "pieceman, rwell " << r << "\t" << radius << std::endl << std::flush;
    // std::cout << "log " << std::log(r/radius) << std::endl << std::flush;
    // std::cout << "trans " << trans << std::endl << std::flush;
    // std::cout << "other "<< 2*M_PI*std::sqrt(k1*k2)*length << std::endl;
    AssertThrow(trans >= 0,
                ExcMessage("productivity <0, probably Cell size is too small, pieceman formula not valid"));
    return trans;
  }  // eom


  template <int dim>
  std::pair<double,double> Wellbore<dim>::get_J_and_Q(const CellIterator<dim> & cell) const
  {
    /*
      Returns a pair of entries of J and Q vectors
     */

    AssertThrow(cells.size() > 0, ExcMessage("Need to locate wells first"));

    const int segment = find_cell(cell);
    if (segment == -1)
      return std::make_pair(0.0, 0.0);

    if (control.type == Schedule::WellControlType::pressure_control)
    {
      return std::make_pair(productivities[segment],
                            control.value*productivities[segment]);
    }
    else if (control.type == Schedule::WellControlType::flow_control_total)
    {
      // compute sum of productivities to normalize flow in a segment
      double sum_productivities = 0;
      for (unsigned int s=0; s<productivities.size(); s++)
        sum_productivities += productivities[s];

      // sum_productivities = Utilities::MPI::sum(sum_productivities, mpi_communicator);

      const double normalized_flux =
          control.value*productivities[segment]/sum_productivities;

      if (sum_productivities > 0)
        return std::make_pair(0.0, normalized_flux);
      else
        return std::make_pair(0.0, 0.0);
    }
    else
    {
      AssertThrow(false, ExcNotImplemented());
    }

    return std::make_pair(0.0, 0.0) ;
  }  // eom

}  // end of namespace
