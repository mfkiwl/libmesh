#include <libmesh/int_range.h>
#include <libmesh/parallel.h>
#include <libmesh/parallel_algebra.h>

#include "test_comm.h"
#include "libmesh_cppunit.h"


using namespace libMesh;

class ParallelPointTest : public CppUnit::TestCase {
public:
  LIBMESH_CPPUNIT_TEST_SUITE( ParallelPointTest );

#if LIBMESH_DIM > 2
  CPPUNIT_TEST( testAllGatherPoint );
  CPPUNIT_TEST( testAllGatherPairPointPoint );
  CPPUNIT_TEST( testAllGatherPairRealPoint );
  CPPUNIT_TEST( testMapUnionGradient );
  CPPUNIT_TEST( testMapUnionPoint );
#endif

  CPPUNIT_TEST( testBroadcastVectorValueInt );
  CPPUNIT_TEST( testBroadcastVectorValueReal );
  CPPUNIT_TEST( testBroadcastPoint );
  CPPUNIT_TEST( testIsendRecv );
  CPPUNIT_TEST( testIrecvSend );

  CPPUNIT_TEST_SUITE_END();

private:

public:
  void setUp()
  {}

  void tearDown()
  {}



  void testAllGatherPoint()
  {
    LOG_UNIT_TEST;

    std::vector<Point> vals;
    Real myrank = TestCommWorld->rank();
    TestCommWorld->allgather(Point(myrank, myrank+0.25, myrank+0.5),vals);

    const std::size_t comm_size = TestCommWorld->size();
    const std::size_t vec_size  = vals.size();
    CPPUNIT_ASSERT_EQUAL( comm_size, vec_size );
    for (processor_id_type i=0; i<vals.size(); i++)
      {
        Real theirrank = i;
        CPPUNIT_ASSERT_EQUAL( theirrank,            vals[i](0) );
        CPPUNIT_ASSERT_EQUAL( theirrank+Real(0.25), vals[i](1) );
        CPPUNIT_ASSERT_EQUAL( theirrank+Real(0.5),  vals[i](2) );
      }
  }



  void testAllGatherPairPointPoint()
  {
    LOG_UNIT_TEST;

    std::vector<std::pair<Point, Point>> vals;
    Real myrank = TestCommWorld->rank();
    TestCommWorld->allgather
      (std::make_pair(Point(myrank, myrank+0.125, myrank+0.25), Point(myrank+0.5, myrank+0.625, myrank+0.75)), vals);

    const std::size_t comm_size = TestCommWorld->size();
    const std::size_t vec_size  = vals.size();
    CPPUNIT_ASSERT_EQUAL( comm_size, vec_size );

    for (processor_id_type i=0; i<vals.size(); i++)
      {
        Real theirrank = i;
        CPPUNIT_ASSERT_EQUAL( theirrank,             vals[i].first(0) );
        CPPUNIT_ASSERT_EQUAL( theirrank+Real(0.125), vals[i].first(1) );
        CPPUNIT_ASSERT_EQUAL( theirrank+Real(0.25),  vals[i].first(2) );
        CPPUNIT_ASSERT_EQUAL( theirrank+Real(0.5),   vals[i].second(0) );
        CPPUNIT_ASSERT_EQUAL( theirrank+Real(0.625), vals[i].second(1) );
        CPPUNIT_ASSERT_EQUAL( theirrank+Real(0.75),  vals[i].second(2) );
      }
  }


  void testAllGatherPairRealPoint()
  {
    LOG_UNIT_TEST;

    std::vector<std::pair<Real, Point>> vals;
    Real myrank = TestCommWorld->rank();
    TestCommWorld->allgather
      (std::make_pair(Real(myrank+0.75), Point(myrank, myrank+0.25, myrank+0.5)), vals);

    const std::size_t comm_size = TestCommWorld->size();
    const std::size_t vec_size  = vals.size();
    CPPUNIT_ASSERT_EQUAL( comm_size, vec_size );

    for (processor_id_type i=0; i<vals.size(); i++)
      {
        Real theirrank = i;
        CPPUNIT_ASSERT_EQUAL( theirrank,            vals[i].second(0) );
        CPPUNIT_ASSERT_EQUAL( theirrank+Real(0.25), vals[i].second(1) );
        CPPUNIT_ASSERT_EQUAL( theirrank+Real(0.5),  vals[i].second(2) );
        CPPUNIT_ASSERT_EQUAL( theirrank+Real(0.75), vals[i].first );
      }
  }



  template <typename VecType>
  void testMapUnionVec()
  {
    // std::map<processor_id_type , std::vector<Point>> vals;
    std::map<processor_id_type , std::vector<VecType>> vals;

    const processor_id_type myrank = TestCommWorld->rank();

    vals[myrank*2].resize(1);
    vals[myrank*2][0](0) = myrank+1;

    TestCommWorld->set_union(vals);

    const processor_id_type comm_size = TestCommWorld->size();

    CPPUNIT_ASSERT_EQUAL(vals.size(), std::size_t(comm_size));
    for (auto p : make_range(comm_size))
      {
        CPPUNIT_ASSERT_EQUAL(vals[p*2].size(), std::size_t(1));
        CPPUNIT_ASSERT_EQUAL(Real(p+1), libmesh_real(vals[p*2][0](0)));
      }
  }


  void testMapUnionGradient()
  {
    LOG_UNIT_TEST;

    testMapUnionVec<Gradient>();
  }


  void testMapUnionPoint()
  {
    LOG_UNIT_TEST;

    testMapUnionVec<Point>();
  }


  template <typename T>
  void testBroadcastVectorValue()
  {
    std::vector<VectorValue<T>> src(3), dest(3);

    {
      T val=T(0);
      for (unsigned int i=0; i<3; i++)
        for (unsigned int j=0; j<LIBMESH_DIM; j++)
          src[i](j) = val++;

      if (TestCommWorld->rank() == 0)
        dest = src;
    }

    TestCommWorld->broadcast(dest);

    for (unsigned int i=0; i<3; i++)
      for (unsigned int j=0; j<LIBMESH_DIM; j++)
        CPPUNIT_ASSERT_EQUAL (src[i](j), dest[i](j) );
  }



  void testBroadcastVectorValueInt()
  {
    LOG_UNIT_TEST;

    this->testBroadcastVectorValue<int>();
  }



  void testBroadcastVectorValueReal()
  {
    LOG_UNIT_TEST;

    this->testBroadcastVectorValue<Real>();
  }



  void testBroadcastPoint()
  {
    LOG_UNIT_TEST;

    std::vector<Point> src(3), dest(3);

    {
      Real val=0.;
      for (unsigned int i=0; i<3; i++)
        for (unsigned int j=0; j<LIBMESH_DIM; j++)
          src[i](j) = val++;

      if (TestCommWorld->rank() == 0)
        dest = src;
    }

    TestCommWorld->broadcast(dest);

    for (unsigned int i=0; i<3; i++)
      for (unsigned int j=0; j<LIBMESH_DIM; j++)
        CPPUNIT_ASSERT_EQUAL (src[i](j), dest[i](j) );
  }



  void testIsendRecv ()
  {
    LOG_UNIT_TEST;

    unsigned int procup = (TestCommWorld->rank() + 1) %
      TestCommWorld->size();
    unsigned int procdown = (TestCommWorld->size() +
                             TestCommWorld->rank() - 1) %
      TestCommWorld->size();

    std::vector<unsigned int> src_val(3), recv_val(3);

    src_val[0] = 0;
    src_val[1] = 1;
    src_val[2] = 2;

    Parallel::Request request;

    if (TestCommWorld->size() > 1)
      {
        // Default communication
        TestCommWorld->send_mode(Parallel::Communicator::DEFAULT);

        TestCommWorld->send (procup,
                             src_val,
                             request);

        TestCommWorld->receive (procdown,
                                recv_val);

        Parallel::wait (request);

        CPPUNIT_ASSERT_EQUAL ( src_val.size() , recv_val.size() );

        for (std::size_t i=0; i<src_val.size(); i++)
          CPPUNIT_ASSERT_EQUAL( src_val[i] , recv_val[i] );


        // Synchronous communication
        TestCommWorld->send_mode(Parallel::Communicator::SYNCHRONOUS);
        std::fill (recv_val.begin(), recv_val.end(), 0);

        TestCommWorld->send (procup,
                             src_val,
                             request);

        TestCommWorld->receive (procdown,
                                recv_val);

        Parallel::wait (request);

        CPPUNIT_ASSERT_EQUAL ( src_val.size() , recv_val.size() );

        for (std::size_t i=0; i<src_val.size(); i++)
          CPPUNIT_ASSERT_EQUAL( src_val[i] , recv_val[i] );

        // Restore default communication
        TestCommWorld->send_mode(Parallel::Communicator::DEFAULT);
      }
  }



  void testIrecvSend ()
  {
    LOG_UNIT_TEST;

    unsigned int procup = (TestCommWorld->rank() + 1) %
      TestCommWorld->size();
    unsigned int procdown = (TestCommWorld->size() +
                             TestCommWorld->rank() - 1) %
      TestCommWorld->size();

    std::vector<unsigned int> src_val(3), recv_val(3);

    src_val[0] = 0;
    src_val[1] = 1;
    src_val[2] = 2;

    Parallel::Request request;

    if (TestCommWorld->size() > 1)
      {
        // Default communication
        TestCommWorld->send_mode(Parallel::Communicator::DEFAULT);

        TestCommWorld->receive (procdown,
                                recv_val,
                                request);

        TestCommWorld->send (procup,
                             src_val);

        Parallel::wait (request);

        CPPUNIT_ASSERT_EQUAL ( src_val.size() , recv_val.size() );

        for (std::size_t i=0; i<src_val.size(); i++)
          CPPUNIT_ASSERT_EQUAL( src_val[i] , recv_val[i] );

        // Synchronous communication
        TestCommWorld->send_mode(Parallel::Communicator::SYNCHRONOUS);
        std::fill (recv_val.begin(), recv_val.end(), 0);


        TestCommWorld->receive (procdown,
                                recv_val,
                                request);

        TestCommWorld->send (procup,
                             src_val);

        Parallel::wait (request);

        CPPUNIT_ASSERT_EQUAL ( src_val.size() , recv_val.size() );

        for (std::size_t i=0; i<src_val.size(); i++)
          CPPUNIT_ASSERT_EQUAL( src_val[i] , recv_val[i] );

        // Restore default communication
        TestCommWorld->send_mode(Parallel::Communicator::DEFAULT);
      }
  }




};

CPPUNIT_TEST_SUITE_REGISTRATION( ParallelPointTest );
