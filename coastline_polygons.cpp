/*

  Copyright 2012-2014 Jochen Topf <jochen@topf.org>.

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

#include <iostream>
#include <vector>
#include <cmath>
#include <assert.h>

#include <ogr_geometry.h>

#include "coastline_polygons.hpp"
#include "output_database.hpp"
#include "osmcoastline.hpp"
#include "srs.hpp"

extern SRS srs;
extern bool debug;

OGRPolygon* CoastlinePolygons::create_rectangular_polygon(double x1, double y1, double x2, double y2, double expand) const {
    OGREnvelope e;

    e.MinX = x1 - expand;
    e.MaxX = x2 + expand;
    e.MinY = y1 - expand;
    e.MaxY = y2 + expand;

    // make sure we are inside the bounds for the output SRS
    e.Intersect(srs.max_extent());

    OGRLinearRing* ring = new OGRLinearRing();
    ring->addPoint(e.MinX, e.MinY);
    ring->addPoint(e.MinX, e.MaxY);
    ring->addPoint(e.MaxX, e.MaxY);
    ring->addPoint(e.MaxX, e.MinY);
    ring->closeRings();

    OGRPolygon* polygon = new OGRPolygon();
    polygon->addRingDirectly(ring);
    polygon->assignSpatialReference(srs.out());

    return polygon;
}

unsigned int CoastlinePolygons::fix_direction() {
    unsigned int warnings = 0;

    for (polygon_vector_t::iterator it = m_polygons->begin(); it != m_polygons->end(); ++it) {
        OGRLinearRing* er = (*it)->getExteriorRing();
        if (!er->isClockwise()) {
            er->reverseWindingOrder();
            // Workaround for bug in OGR: reverseWindingOrder sets dimensions to 3
            er->setCoordinateDimension(2);
            for (int i=0; i < (*it)->getNumInteriorRings(); ++i) {
                (*it)->getInteriorRing(i)->reverseWindingOrder();
                // Workaround for bug in OGR: reverseWindingOrder sets dimensions to 3
                (*it)->getInteriorRing(i)->setCoordinateDimension(2);
            }
            m_output.add_error_line(static_cast<OGRLineString*>(er->clone()), "direction");
            warnings++;
        }
    }

    return warnings;
}

void CoastlinePolygons::transform() {
    for (polygon_vector_t::iterator it = m_polygons->begin(); it != m_polygons->end(); ++it) {
        srs.transform(*it);
    }
}

void CoastlinePolygons::split_geometry(OGRGeometry* geom, int level) {
    if (geom->getGeometryType() == wkbPolygon) {
        geom->assignSpatialReference(srs.out());
        split_polygon(static_cast<OGRPolygon*>(geom), level);
    } else { // wkbMultiPolygon
        for (int i=0; i < static_cast<OGRMultiPolygon*>(geom)->getNumGeometries(); ++i) {
            OGRPolygon* polygon = static_cast<OGRPolygon*>(static_cast<OGRMultiPolygon*>(geom)->getGeometryRef(i));
            polygon->assignSpatialReference(srs.out());
            split_polygon(polygon, level);
        }
    }
}

void CoastlinePolygons::split_polygon(OGRPolygon* polygon, int level) {
    if (level > m_max_split_depth) {
        m_max_split_depth = level;
    }

    int num_points = polygon->getExteriorRing()->getNumPoints();
    if (num_points <= m_max_points_in_polygon) {
        // do not split the polygon if it is small enough
        m_polygons->push_back(polygon);
    } else {
        OGREnvelope envelope;
        polygon->getEnvelope(&envelope);
        if (debug) {
            std::cerr << "DEBUG: split_polygon(): depth="
                      << level
                      << " envelope=("
                      << envelope.MinX << ", " << envelope.MinY
                      << "),("
                      << envelope.MaxX << ", " << envelope.MaxY
                      << ") num_points="
                      << num_points
                      << "\n";
        }

        // These polygons will contain the bounding box of each half of the "polygon" polygon.
        OGRPolygon* b1;
        OGRPolygon* b2;

        if (envelope.MaxX - envelope.MinX < envelope.MaxY-envelope.MinY) {
            if (m_expand >= (envelope.MaxY - envelope.MinY) / 4) {
                std::cerr << "Not splitting polygon with " << num_points << " points on outer ring. It would not get smaller because --bbox-overlap/-b is set to high.\n";
                m_polygons->push_back(polygon);
                return;
            }

            // split vertically
            double MidY = (envelope.MaxY+envelope.MinY) / 2;

            b1 = create_rectangular_polygon(envelope.MinX, envelope.MinY, envelope.MaxX, MidY, m_expand);
            b2 = create_rectangular_polygon(envelope.MinX, MidY, envelope.MaxX, envelope.MaxY, m_expand);
        } else {
            if (m_expand >= (envelope.MaxX - envelope.MinX) / 4) {
                std::cerr << "Not splitting polygon with " << num_points << " points on outer ring. It would not get smaller because --bbox-overlap/-b is set to high.\n";
                m_polygons->push_back(polygon);
                return;
            }

            // split horizontally
            double MidX = (envelope.MaxX+envelope.MinX) / 2;

            b1 = create_rectangular_polygon(envelope.MinX, envelope.MinY, MidX, envelope.MaxY, m_expand);
            b2 = create_rectangular_polygon(MidX, envelope.MinY, envelope.MaxX, envelope.MaxY, m_expand);
        }

        // Use intersection with bbox polygons to split polygon into two halfes
        OGRGeometry* geom1 = polygon->Intersection(b1);
        OGRGeometry* geom2 = polygon->Intersection(b2);

        if (geom1 && (geom1->getGeometryType() == wkbPolygon || geom1->getGeometryType() == wkbMultiPolygon) &&
            geom2 && (geom2->getGeometryType() == wkbPolygon || geom2->getGeometryType() == wkbMultiPolygon)) {
            // split was successful, go on recursively
            split_geometry(geom1, level+1);
            split_geometry(geom2, level+1);
        } else {
            // split was not successful, output some debugging info and keep polygon before split
            std::cerr << "Polygon split at depth " << level << " was not successful. Keeping un-split polygon.\n";
            m_polygons->push_back(polygon);
            if (debug) {
                std::cerr << "DEBUG geom1=" << geom1 << " geom2=" << geom2 << "\n";
                if (geom1 != 0) {
                    std::cerr << "DEBUG geom1 type=" << geom1->getGeometryName() << "\n";
                    if (geom1->getGeometryType() == wkbGeometryCollection) {
                        std::cerr << "DEBUG   numGeometries=" << static_cast<OGRGeometryCollection*>(geom1)->getNumGeometries() << "\n";
                    }
                }
                if (geom2 != 0) {
                    std::cerr << "DEBUG geom2 type=" << geom2->getGeometryName() << "\n";
                    if (geom2->getGeometryType() == wkbGeometryCollection) {
                        std::cerr << "DEBUG   numGeometries=" << static_cast<OGRGeometryCollection*>(geom2)->getNumGeometries() << "\n";
                    }
                }
            }
            delete geom2;
            delete geom1;
        }

        delete b2;
        delete b1;
    }
}

void CoastlinePolygons::split() {
    polygon_vector_t* v = m_polygons;
    m_polygons = new polygon_vector_t;
    m_polygons->reserve(v->size());
    for (polygon_vector_t::iterator it = v->begin(); it != v->end(); ++it) {
        split_polygon(*it, 0);
    }
    delete v;
}

void CoastlinePolygons::output_land_polygons(bool make_copy) {
    if (make_copy) {
        // because adding to a layer destroys the geometry, we need to copy it if it is needed later
        for (polygon_vector_t::iterator it = m_polygons->begin(); it != m_polygons->end(); ++it) {
            m_output.add_land_polygon(static_cast<OGRPolygon*>((*it)->clone()));
        }
    } else {
        for (polygon_vector_t::iterator it = m_polygons->begin(); it != m_polygons->end(); ++it) {
            m_output.add_land_polygon(*it);
        }
    }
}

bool CoastlinePolygons::add_segment_to_line(OGRLineString* line, OGRPoint* point1, OGRPoint* point2) {
    // segments along southern edge of the map are not added to line output
    if (point1->getY() < srs.min_y() && point2->getY() < srs.min_y()) {
        if (debug) {
            std::cerr << "Suppressing segment (" << point1->getX() << " " << point1->getY() << ", " << point2->getX() << " " << point2->getY() << ") near southern edge of map.\n";
        }
        return false;
    }

    // segments along antimeridian are not added to line output
    if ((point1->getX() > srs.max_x() && point2->getX() > srs.max_x()) ||
        (point1->getX() < srs.min_x() && point2->getX() < srs.min_x())) {
        if (debug) {
            std::cerr << "Suppressing segment (" << point1->getX() << " " << point1->getY() << ", " << point2->getX() << " " << point2->getY() << ") near antimeridian.\n";
        }
        return false;
    }

    if (line->getNumPoints() == 0) {
        line->addPoint(point1);
    }
    line->addPoint(point2);
    return true;
}

// Add a coastline ring as LineString to output. Segments in this line that are
// near the southern edge of the map or near the antimeridian are suppressed.
void CoastlinePolygons::output_polygon_ring_as_lines(int max_points, OGRLinearRing* ring) {
    int num = ring->getNumPoints();
    assert(num > 2);

    OGRPoint* point1 = new OGRPoint;
    OGRPoint* point2 = new OGRPoint;
    OGRLineString* line = new OGRLineString;

    ring->getPoint(0, point1);
    for (int i=1; i < num; ++i) {
        ring->getPoint(i, point2);

        bool added = add_segment_to_line(line, point1, point2);

        if (line->getNumPoints() >= max_points || !added) {
            if (line->getNumPoints() >= 2) {
                line->setCoordinateDimension(2);
                line->assignSpatialReference(ring->getSpatialReference());
                m_output.add_line(line);
                line = new OGRLineString;
            }
        }

        point1->setX(point2->getX());
        point1->setY(point2->getY());
    }

    if (line->getNumPoints() >= 2) {
        line->setCoordinateDimension(2);
        line->assignSpatialReference(ring->getSpatialReference());
        m_output.add_line(line);
    } else {
        delete line;
    }

    delete point2;
    delete point1;
}

void CoastlinePolygons::output_lines(int max_points) {
    for (polygon_vector_t::iterator it = m_polygons->begin(); it != m_polygons->end(); ++it) {
        output_polygon_ring_as_lines(max_points, (*it)->getExteriorRing());
        for (int i=0; i < (*it)->getNumInteriorRings(); ++i) {
            output_polygon_ring_as_lines(max_points, (*it)->getInteriorRing(i));
        }
    }
}

void CoastlinePolygons::split_bbox(OGREnvelope e, polygon_vector_t* v) {
//    std::cerr << "envelope = (" << e.MinX << ", " << e.MinY << "), (" << e.MaxX << ", " << e.MaxY << ") v.size()=" << v->size() << "\n";
    if (v->size() < 100) {
        try {
            OGRGeometry* geom = create_rectangular_polygon(e.MinX, e.MinY, e.MaxX, e.MaxY, m_expand);
            assert(geom->getSpatialReference() != NULL);
            for (polygon_vector_t::const_iterator it = v->begin(); it != v->end(); ++it) {
                OGRGeometry* diff = geom->Difference(*it);
                // for some reason there is sometimes no srs on the geometries, so we add them on
                diff->assignSpatialReference(srs.out());
                delete geom;
                geom = diff;
            }
            if (geom) {
                switch (geom->getGeometryType()) {
                    case wkbPolygon:
                        m_output.add_water_polygon(static_cast<OGRPolygon*>(geom));
                        break;
                    case wkbMultiPolygon:
                        for (int i=static_cast<OGRMultiPolygon*>(geom)->getNumGeometries() - 1; i >= 0; --i) {
                            OGRPolygon* p = static_cast<OGRPolygon*>(static_cast<OGRMultiPolygon*>(geom)->getGeometryRef(i));
                            p->assignSpatialReference(geom->getSpatialReference());
                            static_cast<OGRMultiPolygon*>(geom)->removeGeometry(i, FALSE);
                            m_output.add_water_polygon(p);
                        }
                        delete geom;
                        break;
                    case wkbGeometryCollection:
                        // XXX
                        break;
                    default:
                        std::cerr << "IGNORING envelope = (" << e.MinX << ", " << e.MinY << "), (" << e.MaxX << ", " << e.MaxY << ") type=" << geom->getGeometryName() << "\n";
                        // ignore XXX
                        break;
                }
            }
        } catch(...) {
            std::cerr << "ignoring exception\n";
        }
        delete v;
    } else {

        OGREnvelope e1;
        OGREnvelope e2;

        if (e.MaxX - e.MinX < e.MaxY-e.MinY) {
            // split vertically
            double MidY = (e.MaxY+e.MinY) / 2;

            e1.MinX = e.MinX;
            e1.MinY = e.MinY;
            e1.MaxX = e.MaxX;
            e1.MaxY = MidY;

            e2.MinX = e.MinX;
            e2.MinY = MidY;
            e2.MaxX = e.MaxX;
            e2.MaxY = e.MaxY;

        } else {
            // split horizontally
            double MidX = (e.MaxX+e.MinX) / 2;

            e1.MinX = e.MinX;
            e1.MinY = e.MinY;
            e1.MaxX = MidX;
            e1.MaxY = e.MaxY;

            e2.MinX = MidX;
            e2.MinY = e.MinY;
            e2.MaxX = e.MaxX;
            e2.MaxY = e.MaxY;

        }

        polygon_vector_t* v1 = new polygon_vector_t;
        polygon_vector_t* v2 = new polygon_vector_t;
        for (polygon_vector_t::const_iterator it = v->begin(); it != v->end(); ++it) {

            /* You might think re-computing the envelope of all those polygons
            again and again might take a lot of time, but I benchmarked it and
            it has no measurable impact. */
            OGREnvelope e;
            (*it)->getEnvelope(&e);
            if (e1.Intersects(e)) {
                v1->push_back(*it);
            }

            if (e2.Intersects(e)) {
                v2->push_back(*it);
            }
        }
        delete v;
        split_bbox(e1, v1);
        split_bbox(e2, v2);
    }
}


unsigned int CoastlinePolygons::output_water_polygons() {
    unsigned int warnings = 0;
    polygon_vector_t* v = new polygon_vector_t;
    for (polygon_vector_t::iterator it = m_polygons->begin(); it != m_polygons->end(); ++it) {
        OGRPolygon* p = *it;
        if (p->IsValid()) {
            v->push_back(p);
        } else {
            std::cerr << "Invalid polygon, trying buffer(0).\n";
            ++warnings;
            OGRGeometry* buffered_polygon = p->Buffer(0);
            if (buffered_polygon && buffered_polygon->getGeometryType() == wkbPolygon) {
                v->push_back(static_cast<OGRPolygon*>(buffered_polygon));
            } else {
                std::cerr << "Buffer(0) failed, ignoring this polygon. Output data might be invalid!\n";
            }
        }
    }
    split_bbox(srs.max_extent(), v);
    return warnings;
}

