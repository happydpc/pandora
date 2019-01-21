#pragma once
#include <boost/python.hpp>
#include <boost/python/numpy.hpp>
#include <fstream>
#include <string>
#include <string_view>

using ndarray = boost::python::numpy::ndarray;

class PandoraMeshBatch
{
public:
    PandoraMeshBatch(std::string filename);
    ~PandoraMeshBatch();

    boost::python::object addTriangleMesh(
        ndarray npTriangles,
        ndarray npPositions,
        ndarray npNormals,
        ndarray npTangents,
        ndarray npUVCoords,
        boost::python::list transform);
private:
    size_t m_currentPos = 0;
    std::string m_filename;
    std::ofstream m_file;
};
