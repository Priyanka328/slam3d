# Find all dependencies
find_package(PkgConfig REQUIRED)
find_package(Eigen3 REQUIRED)
if (NOT TARGET Eigen3::Eigen)
	add_library(Eigen3::Eigen INTERFACE IMPORTED)
	set_property(TARGET Eigen3::Eigen APPEND PROPERTY
		INTERFACE_INCLUDE_DIRECTORIES ${EIGEN3_INCLUDE_DIR})
endif ()

find_package(Boost REQUIRED COMPONENTS thread graph unit_test_framework)
find_package(PCL 1.7 REQUIRED COMPONENTS registration sample_consensus)
find_package(g2o REQUIRED)
find_package(jsoncpp REQUIRED)

pkg_check_modules(flann REQUIRED IMPORTED_TARGET flann)

# Optional libraries
find_package(libpointmatcher 1.3.1)
find_package(GDAL)
