// This file is part of snark, a generic and flexible library for robotics research
// Copyright (c) 2011 The University of Sydney
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. Neither the name of the University of Sydney nor the
//    names of its contributors may be used to endorse or promote products
//    derived from this software without specific prior written permission.
//
// NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
// GRANTED BY THIS LICENSE.  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
// HOLDERS AND CONTRIBUTORS \"AS IS\" AND ANY EXPRESS OR IMPLIED
// WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
// BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
// OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
// IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

/// @authors vsevolod vlaskine, zhe xu

#include <boost/filesystem/operations.hpp>
#include <comma/application/command_line_options.h>
#include <comma/csv/stream.h>
#include <comma/name_value/ptree.h>
#include <comma/name_value/serialize.h>
#include "../camera/pinhole.h"
#include "../camera/traits.h"

static bool verbose=false;

void usage( bool verbose )
{
    std::cerr << std::endl;
    std::cerr << "take on stdin pixels, perform an operation based on pinhole camera model" << std::endl;
    std::cerr << std::endl;
    std::cerr << "usage: cat pixels.csv | image-pinhole <operation> <options> > points.csv" << std::endl;
    std::cerr << std::endl;
    std::cerr << "operations" << std::endl;
    std::cerr << "    to-cartesian: take on stdin pixels, undistort image, append pixel's cartesian coordinates in camera frame" << std::endl;
    std::cerr << "    to-pixels: take on stdin cartesian coordinates in camera frame, append their coordinates in pixels" << std::endl;
    std::cerr << "    undistort: take on stdin pixels, append their undistorted values" << std::endl;
    std::cerr << "    distort: take on stdin undistorted pixels, append their distorted values (uses distortion map file)" << std::endl;
    std::cerr << "    distortion-map: build distortion map from camera parameters in config and write to stdout (binary image matrix of map x, map y)" << std::endl;
    std::cerr << std::endl;
    std::cerr << "options" << std::endl;
    std::cerr << "    --help,-h: print help; --help --verbose for more help" << std::endl;
    std::cerr << "    --camera-config,--camera,--config,-c=<parameters>: camera configuration" << std::endl;
    std::cerr << "        <parameters>: filename of json configuration file or ';'-separated path-value pairs" << std::endl;
    std::cerr << "                      e.g: --config=\"focal_length/x=123;focal_length/y=123.1;...\"" << std::endl;
    std::cerr << "    --input-fields: output input fields for given operation and exit" << std::endl;
    std::cerr << "    --output-config,--sample-config: output sample config and exit" << std::endl;
    std::cerr << "    --output-fields: output appended fields for given operation and exit" << std::endl;
    std::cerr << "    --output-format: output appended fields binary format for given operation and exit" << std::endl;
    std::cerr << "    --verbose,-v: more output" << std::endl;
    std::cerr << std::endl;
    std::cerr << "camera config: for a sample, try image-pinhole --output-config" << std::endl;
    std::cerr << "    sensor_size/x: sensor width, meters" << std::endl;
    std::cerr << "    sensor_size/y: sensor height, m, meters" << std::endl;
    std::cerr << "    image_size/x: image width, pixels" << std::endl;
    std::cerr << "    image_size/y: image height, pixels" << std::endl;
    std::cerr << "    focal_length: focal length, meters" << std::endl;
    std::cerr << "    principal_point/x: image centre, pixels, default: geometrical image centre" << std::endl;
    std::cerr << "        principal_point/x" << std::endl;
    std::cerr << "        principal_point/y" << std::endl;
    std::cerr << "    distortion parameters" << std::endl;
    std::cerr << "        distortion/radial/k1" << std::endl;
    std::cerr << "        distortion/radial/k2" << std::endl;
    std::cerr << "        distortion/radial/k3" << std::endl;
    std::cerr << "        distortion/tangential/p1" << std::endl;
    std::cerr << "        distortion/tangential/p2" << std::endl;
    std::cerr << "        distortion/map=<filename>: distortion map as two concatenated binary distortion maps for x and y" << std::endl;
    std::cerr << "                                   each map is matrix of floats of size image_size/x X image_size/y" << std::endl;
    std::cerr << std::endl;
    if( verbose ) { std::cerr << comma::csv::options::usage() << std::endl; }
    exit( 0 );
}

template < typename S, typename T > static void output_details( const std::string& operation, const char* expected, const comma::command_line_options& options )
{
    if( operation != expected ) { return; }
    if( options.exists( "--input-fields" ) ) { std::cout << comma::join( comma::csv::names< S >(), ',' ) << std::endl; exit( 0 ); }
    if( options.exists( "--output-fields" ) ) { std::cout << comma::join( comma::csv::names< T >(), ',' ) << std::endl; exit( 0 ); }
    if( options.exists( "--output-format" ) ) { std::cout << comma::csv::format::value< T >() << std::endl; exit( 0 ); }
}

static snark::camera::pinhole make_pinhole( const std::string& config_parameters )
{
    snark::camera::pinhole pinhole;
    if( config_parameters.find_first_of( '=' ) == std::string::npos )
    {
        const std::vector< std::string >& v = comma::split( config_parameters, ":#@" );
        if( v.size() == 1 ) { comma::read_json( pinhole, config_parameters, true ); }
        else { comma::read_json( pinhole, config_parameters.substr( 0, config_parameters.size() - v.back().size() - 1 ), v.back(), true ); }
    }
    else
    {
        boost::property_tree::ptree p;
        comma::property_tree::from_path_value_string( config_parameters, '=', ';', comma::property_tree::path_value::no_check, true );
        comma::from_ptree from_ptree( p, true );
        comma::visiting::apply( from_ptree ).to( pinhole );
    }
    if( !pinhole.principal_point ) { pinhole.principal_point = pinhole.image_centre(); }
    pinhole.init(verbose);
    return pinhole;
}

int main( int ac, char** av )
{
    try
    {
        comma::command_line_options options( ac, av, usage );
        verbose=options.exists("--verbose");
        if( options.exists( "--output-config,--sample-config" ) ) { comma::write_json( snark::camera::pinhole(), std::cout ); return 0; }
        const std::vector< std::string >& unnamed = options.unnamed( "--input-fields,--output-fields,--output-format,--verbose,-v", "-.*" );
        if( unnamed.empty() ) { std::cerr << "image-pinhole: please specify operation" << std::endl; return 1; }
        std::string operation = unnamed[0];
        output_details< Eigen::Vector2d, Eigen::Vector3d >( operation, "to-cartesian", options );
        output_details< Eigen::Vector3d, Eigen::Vector2d >( operation, "to-pixels", options );
        output_details< Eigen::Vector2d, Eigen::Vector2d >( operation, "undistort", options );
        output_details< Eigen::Vector2d, Eigen::Vector2d >( operation, "distort", options );
        snark::camera::pinhole pinhole = make_pinhole( options.value< std::string >( "--camera-config,--camera,--config,-c" ) );
        comma::csv::options csv( options );
        if( operation == "to-cartesian" )
        {
            comma::csv::input_stream< Eigen::Vector2d > is( std::cin, csv );
            comma::csv::output_stream< Eigen::Vector3d > os( std::cout, csv.binary() );
            comma::csv::tied< Eigen::Vector2d, Eigen::Vector3d > tied( is, os );
            while( is.ready() || std::cin.good() )
            {
                const Eigen::Vector2d* p = is.read();
                if( !p ) { break; }
                tied.append( pinhole.to_cartesian( *p ) );
            }
            return 0;
        }
        if( operation == "to-pixels" )
        {
            comma::csv::input_stream< Eigen::Vector3d > is( std::cin, csv );
            comma::csv::output_stream< Eigen::Vector2d > os( std::cout, csv.binary() );
            comma::csv::tied< Eigen::Vector3d, Eigen::Vector2d > tied( is, os );
            while( is.ready() || std::cin.good() )
            {
                const Eigen::Vector3d* p = is.read();
                if( !p ) { break; }
                tied.append( pinhole.to_pixel(*p) );
            }
            return 0;
        }
        if( operation == "undistort" )
        {
            comma::csv::input_stream< Eigen::Vector2d > is( std::cin, csv );
            comma::csv::output_stream< Eigen::Vector2d > os( std::cout, csv.binary() );
            comma::csv::tied< Eigen::Vector2d, Eigen::Vector2d > tied( is, os );
            while( is.ready() || std::cin.good() )
            {
                const Eigen::Vector2d* p = is.read();
                if( !p ) { break; }
                tied.append( pinhole.undistorted( *p ) );
            }
            return 0;
        }
        if ( operation == "distort" )
        {
            comma::csv::input_stream< Eigen::Vector2d > is( std::cin, csv );
            comma::csv::output_stream< Eigen::Vector2d > os( std::cout, csv.binary() );
            comma::csv::tied< Eigen::Vector2d, Eigen::Vector2d > tied( is, os );
            while( is.ready() || std::cin.good() )
            {
                const Eigen::Vector2d* p = is.read();
                if( !p ) { break; }
                tied.append( pinhole.distort( *p ) );
            }
            return 0;
        }
        if(operation=="distortion-map")
        {
            pinhole.output_distortion_map(std::cout);
            return 0;
        }
        std::cerr << "image-pinhole: error: unrecognized operation: "<< operation << std::endl;
        return 1;
    }
    catch( std::exception& ex ) { std::cerr << "image-pinhole: " << ex.what() << std::endl; }
    catch( ... ) { std::cerr << "image-pinhole: unknown exception" << std::endl; }
}
