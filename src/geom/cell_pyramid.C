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

// Local includes
#include "libmesh/cell_pyramid.h"
#include "libmesh/cell_pyramid5.h"
#include "libmesh/face_tri3.h"
#include "libmesh/face_quad4.h"

namespace libMesh
{

// ------------------------------------------------------------
// Pyramid class static member initializations
const int Pyramid::num_sides;
const int Pyramid::num_edges;
const int Pyramid::num_children;

const Real Pyramid::_master_points[14][3] =
  {
    {-1, -1, 0},
    {1, -1, 0},
    {1, 1, 0},
    {-1, 1, 0},
    {0, 0, 1},
    {0, -1, 0},
    {1, 0, 0},
    {0, 1, 0},
    {-1, 0, 0},
    {0, -0.5, 0.5},
    {0.5, 0, 0.5},
    {0, 0.5, 0.5},
    {-0.5, 0, 0.5},
    {0, 0, 0}
  };

const unsigned int Pyramid::edge_sides_map[8][2] =
  {
    {0, 4}, // Edge 0
    {1, 4}, // Edge 1
    {2, 4}, // Edge 2
    {3, 4}, // Edge 3
    {0, 3}, // Edge 4
    {0, 1}, // Edge 5
    {1, 2}, // Edge 6
    {2, 3}  // Edge 7
  };

const unsigned int Pyramid::adjacent_edges_map[/*num_vertices*/5][/*max_adjacent_edges*/4] =
  {
    {0, 3, 4, 99},  // Edges adjacent to node 0
    {0, 1, 5, 99},  // Edges adjacent to node 1
    {1, 2, 6, 99},  // Edges adjacent to node 2
    {2, 3, 7, 99},  // Edges adjacent to node 3
    {4, 5, 6,  7}   // Edges adjacent to node 4
  };

// ------------------------------------------------------------
// Pyramid class member functions
dof_id_type Pyramid::key (const unsigned int s) const
{
  libmesh_assert_less (s, this->n_sides());

  switch (s)
    {
    case 0: // triangular face 1
    case 1: // triangular face 2
    case 2: // triangular face 3
    case 3: // triangular face 4
      return this->compute_key (this->node_id(Pyramid5::side_nodes_map[s][0]),
                                this->node_id(Pyramid5::side_nodes_map[s][1]),
                                this->node_id(Pyramid5::side_nodes_map[s][2]));

    case 4:  // the quad face at z=0
      return this->compute_key (this->node_id(Pyramid5::side_nodes_map[s][0]),
                                this->node_id(Pyramid5::side_nodes_map[s][1]),
                                this->node_id(Pyramid5::side_nodes_map[s][2]),
                                this->node_id(Pyramid5::side_nodes_map[s][3]));

    default:
      libmesh_error_msg("Invalid side s = " << s);
    }
}



dof_id_type Pyramid::low_order_key (const unsigned int s) const
{
  libmesh_assert_less (s, this->n_sides());

  switch (s)
    {
    case 0: // triangular face 1
    case 1: // triangular face 2
    case 2: // triangular face 3
    case 3: // triangular face 4
      return this->compute_key (this->node_id(Pyramid5::side_nodes_map[s][0]),
                                this->node_id(Pyramid5::side_nodes_map[s][1]),
                                this->node_id(Pyramid5::side_nodes_map[s][2]));

    case 4:  // the quad face at z=0
      return this->compute_key (this->node_id(Pyramid5::side_nodes_map[s][0]),
                                this->node_id(Pyramid5::side_nodes_map[s][1]),
                                this->node_id(Pyramid5::side_nodes_map[s][2]),
                                this->node_id(Pyramid5::side_nodes_map[s][3]));

    default:
      libmesh_error_msg("Invalid side s = " << s);
    }
}



unsigned int Pyramid::local_side_node(unsigned int side,
                                      unsigned int side_node) const
{
  libmesh_assert_less (side, this->n_sides());

  // Never more than 4 nodes per side.
  libmesh_assert_less(side_node, Pyramid5::nodes_per_side);

  // Some sides have 3 nodes.
  libmesh_assert(side == 4 || side_node < 3);

  return Pyramid5::side_nodes_map[side][side_node];
}



unsigned int Pyramid::local_edge_node(unsigned int edge,
                                      unsigned int edge_node) const
{
  libmesh_assert_less(edge, this->n_edges());
  libmesh_assert_less(edge_node, Pyramid5::nodes_per_edge);

  return Pyramid5::edge_nodes_map[edge][edge_node];
}



std::unique_ptr<Elem> Pyramid::side_ptr (const unsigned int i)
{
  libmesh_assert_less (i, this->n_sides());

  // Return value
  std::unique_ptr<Elem> face;

  // Set up the type of element
  switch (i)
    {
    case 0: // triangular face 1
    case 1: // triangular face 2
    case 2: // triangular face 3
    case 3: // triangular face 4
      {
        face = std::make_unique<Tri3>();
        break;
      }
    case 4:  // the quad face at z=0
      {
        face = std::make_unique<Quad4>();
        break;
      }
    default:
      libmesh_error_msg("Invalid side i = " << i);
    }

  // Set the nodes
  for (auto n : face->node_index_range())
    face->set_node(n) = this->node_ptr(Pyramid5::side_nodes_map[i][n]);

  return face;
}



void Pyramid::side_ptr (std::unique_ptr<Elem> & side,
                        const unsigned int i)
{
  libmesh_assert_less (i, this->n_sides());

  switch (i)
    {
    case 0: // triangular face 1
    case 1: // triangular face 2
    case 2: // triangular face 3
    case 3: // triangular face 4
      {
        if (!side.get() || side->type() != TRI3)
          {
            side = this->side_ptr(i);
            return;
          }
        break;
      }

    case 4:  // the quad face at z=0
      {
        if (!side.get() || side->type() != QUAD4)
          {
            side = this->side_ptr(i);
            return;
          }
        break;
      }

    default:
      libmesh_error_msg("Invalid side i = " << i);
    }

  side->subdomain_id() = this->subdomain_id();

  // Set the nodes
  for (auto n : side->node_index_range())
    side->set_node(n) = this->node_ptr(Pyramid5::side_nodes_map[i][n]);
}



bool Pyramid::is_child_on_side(const unsigned int c,
                               const unsigned int s) const
{
  libmesh_assert_less (c, this->n_children());
  libmesh_assert_less (s, this->n_sides());

  for (unsigned int i = 0; i != 4; ++i)
    if (Pyramid5::side_nodes_map[s][i] == c)
      return true;
  return false;
}



bool Pyramid::is_edge_on_side(const unsigned int e,
                              const unsigned int s) const
{
  libmesh_assert_less (e, this->n_edges());
  libmesh_assert_less (s, this->n_sides());

  return (edge_sides_map[e][0] == s || edge_sides_map[e][1] == s);
}



std::vector<unsigned int> Pyramid::sides_on_edge(const unsigned int e) const
{
  libmesh_assert_less (e, this->n_edges());
  return {edge_sides_map[e][0], edge_sides_map[e][1]};
}


bool
Pyramid::is_flipped() const
{
  return (triple_product(this->point(1)-this->point(0),
                         this->point(3)-this->point(0),
                         this->point(4)-this->point(0)) < 0);
}

std::vector<unsigned int>
Pyramid::edges_adjacent_to_node(const unsigned int n) const
{
  libmesh_assert_less(n, this->n_nodes());
  if (this->is_vertex(n))
    {
      auto trim = (n < 4) ? 1 : 0;
      return {std::begin(adjacent_edges_map[n]), std::end(adjacent_edges_map[n]) - trim};
    }
  else if (this->is_edge(n))
    return {n - this->n_vertices()};

  // Not a vertex or edge node, so must be one of the face nodes.
  libmesh_assert(this->is_face(n));
  return {};
}

unsigned int Pyramid::local_singular_node(const Point & p, const Real tol) const
{
  return this->node_ref(4).absolute_fuzzy_equals(p, tol) ? 4 : invalid_uint;
}


bool Pyramid::on_reference_element(const Point & p,
                                   const Real eps) const
{
  const Real & xi = p(0);
  const Real & eta = p(1);
  const Real & zeta = p(2);

  // Check that the point is on the same side of all the faces
  // by testing whether:
  //
  // n_i.(x - x_i) <= 0
  //
  // for each i, where:
  //   n_i is the outward normal of face i,
  //   x_i is a point on face i.
  return ((-eta - 1. + zeta <= 0.+eps) &&
          (  xi - 1. + zeta <= 0.+eps) &&
          ( eta - 1. + zeta <= 0.+eps) &&
          ( -xi - 1. + zeta <= 0.+eps) &&
          (            zeta >= 0.-eps));
}


} // namespace libMesh
