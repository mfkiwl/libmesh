#include <libmesh/libmesh.h>
#include <libmesh/replicated_mesh.h>
#include <libmesh/elem.h>
#include <libmesh/mesh_generation.h>
#include <libmesh/mesh_modification.h>
#include <libmesh/boundary_info.h>

#include "test_comm.h"
#include "libmesh_cppunit.h"


using namespace libMesh;

class AllTriTest : public CppUnit::TestCase
{
  /**
   * The goal of this test is to verify proper operation of the Mesh Extruder
   * with the optional object callback for setting custom subdomain IDs.
   * We pass a custom object for generating subdomains based on the old element
   * ID and the current layer and assert the proper values.
   */
public:
  LIBMESH_CPPUNIT_TEST_SUITE( AllTriTest );

  // 2D tests
#if LIBMESH_DIM > 1
  CPPUNIT_TEST( testAllTriTri );
  CPPUNIT_TEST( testAllTriQuad );
  CPPUNIT_TEST( testAllTriQuad8 );
  CPPUNIT_TEST( testAllTriQuad9 );
#endif

  // 3D tests
#if LIBMESH_DIM > 2
  CPPUNIT_TEST( testAllTriPrism6 );
  CPPUNIT_TEST( testAllTriPrism18 );
  CPPUNIT_TEST( testAllTriPrism20 );
  CPPUNIT_TEST( testAllTriPrism21 );
#endif

  CPPUNIT_TEST_SUITE_END();

protected:
  // Helper function called by the test implementations, saves a few lines of code.
  void test_helper_2D(ElemType elem_type,
                      dof_id_type n_elem_expected,
                      std::size_t n_boundary_conds_expected)
  {
    ReplicatedMesh mesh(*TestCommWorld, /*dim=*/2);

    // Build a 2x1 TRI3 mesh and ask to split it into triangles.
    // Should be a no-op
    MeshTools::Generation::build_square(mesh,
                                        /*nx=*/2, /*ny=*/1,
                                        /*xmin=*/0., /*xmax=*/1.,
                                        /*ymin=*/0., /*ymax=*/1.,
                                        elem_type);

    MeshTools::Modification::all_tri(mesh);

    // Make sure that the expected number of elements is found.
    CPPUNIT_ASSERT_EQUAL(n_elem_expected, mesh.n_elem());

    // Make sure the expected number of BCs is found.
    CPPUNIT_ASSERT_EQUAL(n_boundary_conds_expected, mesh.get_boundary_info().n_boundary_conds());
  }

  // Helper function called by the test implementations in 3D, saves a few lines of code.
  void test_helper_3D(ElemType elem_type,
                      dof_id_type n_elem_expected,
                      std::size_t n_boundary_conds_expected)
  {
    ReplicatedMesh mesh(*TestCommWorld, /*dim=*/3);

    // Build a 2x1 TRI3 mesh and ask to split it into triangles.
    // Should be a no-op
    MeshTools::Generation::build_cube(mesh,
                                      /*nx=*/1, /*ny=*/1, /*nz=*/1,
                                      /*xmin=*/0., /*xmax=*/1.,
                                      /*ymin=*/0., /*ymax=*/1.,
                                      /*zmin=*/0., /*zmax=*/1.,
                                      elem_type);

    MeshTools::Modification::all_tri(mesh);

    // Make sure that the expected number of elements is found.
    CPPUNIT_ASSERT_EQUAL(n_elem_expected, mesh.n_elem());

    // Make sure the expected number of BCs is found.
    CPPUNIT_ASSERT_EQUAL(n_boundary_conds_expected, mesh.get_boundary_info().n_boundary_conds());
  }

public:
  void setUp() {}

  void tearDown() {}

  // 4 TRIs no-op
  void testAllTriTri() { LOG_UNIT_TEST; test_helper_2D(TRI3, /*nelem=*/4, /*nbcs=*/6); }

  // 2 quads split into 4 TRIs.
  void testAllTriQuad() { LOG_UNIT_TEST; test_helper_2D(QUAD4, /*nelem=*/4, /*nbcs=*/6); }

  // 2 QUAD8s split into 4 TRIs.
  void testAllTriQuad8() { LOG_UNIT_TEST; test_helper_2D(QUAD8, /*nelem=*/4, /*nbcs=*/6); }

  // 2 QUAD9s split into 4 TRIs.
  void testAllTriQuad9() { LOG_UNIT_TEST; test_helper_2D(QUAD9, /*nelem=*/4, /*nbcs=*/6); }

  // 2 PRISMs split into 6 TETs with 2 boundary faces per side.
  void testAllTriPrism6() { LOG_UNIT_TEST; test_helper_3D(PRISM6, /*nelem=*/6, /*nbcs=*/12); }
  void testAllTriPrism18() { LOG_UNIT_TEST; test_helper_3D(PRISM18, /*nelem=*/6, /*nbcs=*/12); }
  void testAllTriPrism20() { LOG_UNIT_TEST; test_helper_3D(PRISM20, /*nelem=*/6, /*nbcs=*/12); }
  void testAllTriPrism21() { LOG_UNIT_TEST; test_helper_3D(PRISM21, /*nelem=*/6, /*nbcs=*/12); }
};


CPPUNIT_TEST_SUITE_REGISTRATION( AllTriTest );
