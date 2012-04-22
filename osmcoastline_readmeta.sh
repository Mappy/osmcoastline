#!/bin/sh
#
#  osmcoastline_readmeta [COASTLINEDB]
#

if [ "x$1" = "x" ]; then
    DBFILE=testdata.db
else
    DBFILE=$1
fi

echo "Options used to create this data:\n"

echo -n "  Overlap (--bbox-overlap/-b): "
sqlite3 $DBFILE "SELECT overlap FROM options;"

echo -n "  Close gaps in coastline smaller than (--close-distance/-c): "
sqlite3 $DBFILE "SELECT close_distance FROM options;"

echo -n "  Max points in polygons (--max-points/-m): "
sqlite3 $DBFILE "SELECT max_points_in_polygons FROM options;"

echo -n "  Split large polygons: "
sqlite3 $DBFILE "SELECT CASE split_large_polygons WHEN 0 THEN 'no' ELSE 'yes' END FROM options;"

echo -n "  Create water polygons (--water/-w): "
sqlite3 $DBFILE "SELECT CASE water WHEN 0 THEN 'no' ELSE 'yes' END FROM options;"

echo -n "  Spatial reference system (--srid/-s): "
sqlite3 $DBFILE "SELECT CASE srid WHEN 4326 THEN '4326 (WGS84)' WHEN 3857 THEN '3857 (Mercator)' ELSE srid END FROM geometry_columns WHERE f_table_name='polygons';"

echo "\nMetadata:\n"

echo -n "  Database created at: "
sqlite3 $DBFILE "SELECT timestamp FROM meta;"

echo -n "  Runtime (minutes): "
sqlite3 $DBFILE "SELECT CAST(round(CAST(runtime AS REAL)/60) AS INT) FROM meta;"

echo -n "  Memory usage (MB): "
sqlite3 $DBFILE "SELECT memory_usage FROM meta;"

echo -n "  Ways tagged natural=coastline: "
sqlite3 $DBFILE "SELECT num_ways FROM meta;"

echo -n "  Number of nodes where coastline is not closed: "
sqlite3 $DBFILE "SELECT num_unconnected_nodes FROM meta;"

echo -n "  Coastline rings: "
sqlite3 $DBFILE "SELECT num_coastline_rings FROM meta;"

echo -n "  Coastline rings created from a single way: "
sqlite3 $DBFILE "SELECT num_coastline_rings_from_single_way FROM meta;"

echo -n "  Coastline rings created from more then one way: "
sqlite3 $DBFILE "SELECT num_coastline_rings - num_coastline_rings_from_single_way FROM meta;"

echo -n "  Number of land polygons before split: "
sqlite3 $DBFILE "SELECT num_land_polygons_before_split FROM meta;"

echo -n "  Number of land polygons after split (0 if not split): "
sqlite3 $DBFILE "SELECT num_land_polygons_after_split FROM meta;"

echo "\nErrors/warnings on points:\n"
echo ".width 3 20\nSELECT count(*), error FROM error_points GROUP BY error;" | sqlite3 -column $DBFILE | sed -e 's/^/  /'

echo "\nErrors/warnings on linestrings:\n"
echo ".width 3 20\nSELECT count(*), error FROM error_lines GROUP BY error;" | sqlite3 -column $DBFILE | sed -e 's/^/  /'

