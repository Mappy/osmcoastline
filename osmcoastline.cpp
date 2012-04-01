/*

  Copyright 2012 Jochen Topf <jochen@topf.org>.

  This file is part of OSMCoastline.

  OSMCoastline is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  OSMCoastline is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OSMCoastline.  If not, see <http://www.gnu.org/licenses/>.

*/

#include <cstdlib>
#include <fstream>
#include <list>
#include <time.h>
#include <getopt.h>

#include <osmium.hpp>
#include <osmium/geometry/point.hpp>

#include "osmcoastline.hpp"
#include "coastline_ring.hpp"
#include "coastline_ring_collection.hpp"
#include "output_database.hpp"
#include "output_layers.hpp"

#include "options.hpp"
#include "coastline_handlers.hpp"

/* ================================================== */

/**
 * This function assembles all the coastline rings into one huge multipolygon.
 */
OGRMultiPolygon* create_polygons(CoastlineRingCollection coastline_rings) {
    std::vector<OGRGeometry*> all_polygons;
    coastline_rings.add_polygons_to_vector(all_polygons);

    int is_valid;
    const char* options[] = { "METHOD=ONLY_CCW", NULL };
    OGRGeometry* mega_multipolygon = OGRGeometryFactory::organizePolygons(&all_polygons[0], all_polygons.size(), &is_valid, options);

    assert(mega_multipolygon->getGeometryType() == wkbMultiPolygon);

    return static_cast<OGRMultiPolygon*>(mega_multipolygon);
}

/**
 * Write all polygons in the given multipolygon as polygons to the output database.
 */
void output_polygons(OGRMultiPolygon* multipolygon, OutputDatabase& output) {
    for (int i=0; i < multipolygon->getNumGeometries(); ++i) {
        OGRPolygon* p = static_cast<OGRPolygon*>(multipolygon->getGeometryRef(i));
        output.layer_polygons()->add(p, p->getExteriorRing()->isClockwise());
    }
}

OGRPolygon* create_rectangular_polygon(double x1, double y1, double x2, double y2, double expand) {
    x1 -= expand;
    x2 += expand;
    y1 -= expand;
    y2 += expand;

    if (x1 < -180) { x1 = -180; }
    if (x1 >  180) { x1 =  180; }
    if (x2 < -180) { x2 = -180; }
    if (x2 >  180) { x2 =  180; }

    if (y1 <  -90) { y1 =  -90; }
    if (y1 >   90) { y2 =   90; }
    if (y2 <  -90) { y2 =  -90; }
    if (y2 >   90) { y2 =   90; }

    OGRLinearRing* ring = new OGRLinearRing();
    ring->addPoint(x1, y1);
    ring->addPoint(x1, y2);
    ring->addPoint(x2, y2);
    ring->addPoint(x2, y1);
    ring->closeRings();

    OGRPolygon* polygon = new OGRPolygon();
    polygon->addRingDirectly(ring);

    return polygon;
}

void split(OGRGeometry* g, OutputDatabase& output, const Options& options) {
    static double expand = 0.0001;
    OGRPolygon* p = static_cast<OGRPolygon*>(g);

    if (p->getExteriorRing()->getNumPoints() <= options.max_points_in_polygons) {
        output.layer_polygons()->add(p, 0); // XXX p->getExteriorRing()->isClockwise());
    } else {
        OGREnvelope envelope;
        p->getEnvelope(&envelope);

        OGRPolygon* b1;
        OGRPolygon* b2;

        if (envelope.MaxX - envelope.MinX < envelope.MaxY-envelope.MinY) {
            // split vertically
            double MidY = (envelope.MaxY+envelope.MinY) / 2;

            b1 = create_rectangular_polygon(envelope.MinX, envelope.MinY, envelope.MaxX, MidY, expand);
            b2 = create_rectangular_polygon(envelope.MinX, MidY, envelope.MaxX, envelope.MaxY, expand);
        } else {
            // split horizontally
            double MidX = (envelope.MaxX+envelope.MinX) / 2;

            b1 = create_rectangular_polygon(envelope.MinX, envelope.MinY, MidX, envelope.MaxY, expand);
            b2 = create_rectangular_polygon(MidX, envelope.MinY, envelope.MaxX, envelope.MaxY, expand);
        }

        OGRGeometry* g1 = p->Intersection(b1);
        OGRGeometry* g2 = p->Intersection(b2);

        if (g1 && (g1->getGeometryType() == wkbPolygon || g1->getGeometryType() == wkbMultiPolygon) &&
            g2 && (g2->getGeometryType() == wkbPolygon || g2->getGeometryType() == wkbMultiPolygon)) {
            if (g1->getGeometryType() == wkbPolygon) {
                split(g1, output, options);
            } else {
                OGRMultiPolygon* mp = static_cast<OGRMultiPolygon*>(g1);
                for (int i=0; i < mp->getNumGeometries(); ++i) {
                    OGRPolygon* gp = static_cast<OGRPolygon*>(mp->getGeometryRef(i));
                    split(gp, output, options);
                }
            }
            if (g2->getGeometryType() == wkbPolygon) {
                split(g2, output, options);
            } else {
                OGRMultiPolygon* mp = static_cast<OGRMultiPolygon*>(g2);
                for (int i=0; i < mp->getNumGeometries(); ++i) {
                    OGRPolygon* gp = static_cast<OGRPolygon*>(mp->getGeometryRef(i));
                    split(gp, output, options);
                }
            }
        } else {
            std::cerr << "g1=" << g1 << " g2=" << g2 << "\n";
            if (g1 != 0) {
                std::cerr << "g1 type=" << g1->getGeometryName() << "\n";
            }
            if (g2 != 0) {
                std::cerr << "g2 type=" << g2->getGeometryName() << "\n";
            }
            output.layer_polygons()->add(p, 1);
            delete g2;
            delete g1;
        }

        delete b2;
        delete b1;
    }
}

/* ================================================== */

unsigned int fix_coastline_direction(OGRMultiPolygon* multipolygon, OutputDatabase& output) {
    unsigned int warnings = 0;

    for (int i=0; i < multipolygon->getNumGeometries(); ++i) {
        OGRPolygon* p = static_cast<OGRPolygon*>(multipolygon->getGeometryRef(i));
        if (!p->getExteriorRing()->isClockwise()) {
            p->getExteriorRing()->reverseWindingOrder();
            OGRLineString* l = static_cast<OGRLineString*>(p->getExteriorRing()->clone());
            output.add_error(l, "direction");
            warnings++;
        }
    }

    return warnings;
}

/* ================================================== */

void output_split_polygons(OGRMultiPolygon* multipolygon, OutputDatabase& output, const Options& options) {
    for (int i=0; i < multipolygon->getNumGeometries(); ++i) {
        OGRPolygon* p = static_cast<OGRPolygon*>(multipolygon->getGeometryRef(i));
        if (p->IsValid()) {
            split(p, output, options);
        } else {
            OGRPolygon* pb = dynamic_cast<OGRPolygon*>(p->Buffer(0));
            if (pb) {
                std::cerr << "not valid, but buffer ok\n";
                split(pb, output, options);
            } else {
                std::cerr << "not valid, and buffer not polygon\n";
            }
        }
    }
}

/* ================================================== */

void print_memory_usage() {
    char filename[100];
    sprintf(filename, "/proc/%d/status", getpid());
    std::ifstream status_file(filename);
    std::string line;

    if (status_file.is_open()) {
        while (! status_file.eof() ) {
            std::getline(status_file, line);
            if (line.substr(0, 6) == "VmPeak" || line.substr(0, 6) == "VmSize") {
                std::cerr << line << std::endl;
            }
        }
        status_file.close();
    }
}

/* ================================================== */

int main(int argc, char *argv[]) {
    time_t start_time = time(NULL);
    unsigned int warnings = 0;
    unsigned int errors = 0;

    Options options(argc, argv);

    Osmium::init(options.debug);

    if (options.debug) {
        std::cerr << "Reading from file '" << options.osmfile << "'.\n";
    }
    Osmium::OSMFile infile(options.osmfile);

    Osmium::OSMFile* outfile = NULL;
    Osmium::Output::Base* raw_output = NULL;
    if (options.raw_output.empty()) {
        if (options.debug) {
            std::cerr << "Not writing raw output.\n";
        }
    } else {
        if (options.debug) {
            std::cerr << "Writing raw output to file '" << options.raw_output << "'.\n";
        }
        outfile = new Osmium::OSMFile(options.raw_output);
        raw_output = outfile->create_output_file();
    }

    OutputDatabase* output = NULL;
    if (!options.outdb.empty()) { 
        if (options.debug) {
            std::cerr << "Using SRS " << options.epsg << " for output.\n";
            if (options.create_index) {
                std::cerr << "Will create geometry index.\n";
            } else {
                std::cerr << "Will NOT create geometry index.\n";
            }
        }

        output = new OutputDatabase(options.outdb, options.epsg, options.create_index);
    }

    CoastlineRingCollection coastline_rings;

    std::cerr << "-------------------------------------------------------------------------------\n";
    std::cerr << "Reading ways (1st pass through input file)...\n";
    time_t t = time(NULL);
    CoastlineHandlerPass1 handler_pass1(raw_output, coastline_rings);
    infile.read(handler_pass1);
    std::cerr << "Done (about " << (time(NULL)-t)/60 << " minutes).\n";
    print_memory_usage();

    warnings += handler_pass1.warnings();
    errors += handler_pass1.errors();

    std::cerr << "-------------------------------------------------------------------------------\n";
    std::cerr << "Reading nodes (2nd pass through input file)...\n";
    t = time(NULL);
    CoastlineHandlerPass2 handler_pass2(raw_output, coastline_rings, output);
    infile.read(handler_pass2);
    std::cerr << "Done (about " << (time(NULL)-t)/60 << " minutes).\n";
    print_memory_usage();

    warnings += handler_pass2.warnings();
    errors += handler_pass2.errors();

    if (options.close_rings) {
        coastline_rings.close_rings();
    }

    if (output) {
        std::cerr << "-------------------------------------------------------------------------------\n";
        std::cerr << "Create and output polygons...\n";
        t = time(NULL);
        if (options.output_rings) {
            warnings += coastline_rings.output_rings(*output);
        }
        if (options.output_polygons) {
            OGRMultiPolygon* mp = create_polygons(coastline_rings);
            warnings += fix_coastline_direction(mp, *output);
            if (options.split_large_polygons) {
                output_split_polygons(mp, *output, options);
            } else {
                output_polygons(mp, *output);
            }
        }
        std::cerr << "Done (about " << (time(NULL)-t)/60 << " minutes).\n";
        print_memory_usage();
    }

    std::cerr << "-------------------------------------------------------------------------------\n";

    output->set_meta(time(NULL) - start_time, 0);

    delete output;
    delete raw_output;
    delete outfile;

    if (warnings) {
        std::cerr << "There were " << warnings << " warnings.\n";
    }
    if (errors) {
        std::cerr << "There were " << errors << " errors.\n";
    }
    if (errors) {
        return return_code_error;
    } else if (warnings) {
        return return_code_warning;
    } else {
        return return_code_ok;
    }
}

