// SPDX-FileCopyrightText: 2026 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#include "arguments.h"
#include "argumentsbcreader.h"
#include "argumentsbcwriter.h"
#include "argumentsreader.h"
#include "argumentswriter.h"
#include "argumentsbcreader.h"
#include "error.h"

#include <chrono>
#include <iostream>

int main(int, char *[])
{
    // Simple test
    {
        ArgumentsWriter writer;
        writer.writeString("Hello, world!");
        Arguments args = writer.finish();

        ArgumentsBcReader reader(args);
        std::cout << "String from BcReader is: \"" << reader.readString().ptr << "\"\n";
    }


    // Demo and benchmark with a signature taken from an existing machine control application.
    // It's about a processing program made of tool paths, made of 2D waypoints, with an optional list of
    // attributes at each waypoint.

    // Write Arguments - real-world type signature "(ia(ia(dda(id))))"
    Arguments args;
    {
        auto start = std::chrono::high_resolution_clock::now();

        ArgumentsWriter writer;
        writer.beginStruct();
            writer.writeInt32(1); // program index
            writer.beginArray(); // list of paths
                for (uint32 iPath = 0; iPath < 20; iPath++) {
                    writer.beginStruct();
                        writer.writeInt32(iPath); // path index
                        writer.beginArray(); // list of waypoints
                            for (uint32 iWaypoint = 0; iWaypoint < 2000; iWaypoint++) {
                                writer.beginStruct();
                                    writer.writeDouble(10.0); // x coordinate
                                    writer.writeDouble(10.0); // y coordinate

                                    // write two waypoint attributes for every 10th waypoint
                                    const uint32 attrCount = (iWaypoint % 10 == 0) ? 2 : 0;
                                    // list of waypoint attributes
                                    writer.beginArray(attrCount ? ArgumentsWriter::NonEmptyArray
                                                                : ArgumentsWriter::WriteTypesOfEmptyArray);
                                        for (uint32 iAttr = 0; iAttr == 0 || iAttr < attrCount; iAttr++) {
                                            writer.beginStruct();
                                                writer.writeInt32(iAttr); // attribute ID
                                                writer.writeDouble(iAttr ? 23.0 : 42.0); // attribute value
                                            writer.endStruct();
                                        }
                                    writer.endArray(); // list of waypoint attributes
                                writer.endStruct();
                            }
                        writer.endArray(); // list of waypoints
                    writer.endStruct();
                }
            writer.endArray(); // list of paths
        writer.endStruct();
        args = writer.finish();

        auto stop = std::chrono::high_resolution_clock::now();
        std::cout << "ArgumentsWriter took "
                  << std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count()
                  << " microseconds\n";
    }
    std::cout << "Serialized size is " << args.data().length / 1024 << " KiB.\n";

    // BcWriter no elided structs
    {
        auto start = std::chrono::high_resolution_clock::now();

        ArgumentsBcWriter writer(args.signature());
        writer.beginStruct();
            writer.writeInt32(1); // program index
            writer.beginArray(); // list of paths
                for (uint32 iPath = 0; iPath < 20; iPath++) {
                    writer.beginStruct();
                        writer.writeInt32(iPath); // path index
                        writer.beginArray(); // list of waypoints
                            for (uint32 iWaypoint = 0; iWaypoint < 2000; iWaypoint++) {
                                writer.beginStruct();
                                    writer.writeDouble(10.0); // x coordinate
                                    writer.writeDouble(10.0); // y coordinate

                                    // write two waypoint attributes for every 10th waypoint
                                    const uint32 attrCount = (iWaypoint % 10 == 0) ? 2 : 0;
                                    // list of waypoint attributes
                                    writer.beginArray();
                                        for (uint32 iAttr = 0; iAttr < attrCount; iAttr++) {
                                            writer.beginStruct();
                                                writer.writeInt32(iAttr); // attribute ID
                                                writer.writeDouble(iAttr ? 23.0 : 42.0); // attribute value
                                            writer.endStruct();
                                        }
                                    writer.endArray(); // list of waypoint attributes
                                writer.endStruct();
                            }
                        writer.endArray(); // list of waypoints
                    writer.endStruct();
                }
            writer.endArray(); // list of paths
        writer.endStruct();
        Arguments args2 = writer.finish();

        auto stop = std::chrono::high_resolution_clock::now();
        std::cout << "ArgumentsBcWriter took "
                  << std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count()
                  << " microseconds\n";
    }

    // BcWriter elided structs
    {
        auto start = std::chrono::high_resolution_clock::now();

        ArgumentsBcWriter writer(args.signature(), Arguments::FerEncodeOptions::ElideStructs);
        writer.writeInt32(1); // program index
        writer.beginArray(); // list of paths
            for (uint32 iPath = 0; iPath < 20; iPath++) {
                writer.writeInt32(iPath); // path index
                writer.beginArray(); // list of waypoints
                    for (uint32 iWaypoint = 0; iWaypoint < 2000; iWaypoint++) {
                        writer.writeDouble(10.0); // x coordinate
                        writer.writeDouble(10.0); // y coordinate

                        // write two waypoint attributes for every 10th waypoint
                        const uint32 attrCount = (iWaypoint % 10 == 0) ? 2 : 0;
                        // list of waypoint attributes
                        writer.beginArray();
                        for (uint32 iAttr = 0; iAttr < attrCount; iAttr++) {
                            writer.writeInt32(iAttr); // attribute ID
                            writer.writeDouble(iAttr ? 23.0 : 42.0); // attribute value
                        }
                        writer.endArray(); // list of waypoint attributes
                    }
                writer.endArray(); // list of waypoints
            }
        writer.endArray(); // list of paths
        Arguments args2 = writer.finish();

        auto stop = std::chrono::high_resolution_clock::now();
        std::cout << "ArgumentsBcWriter (elided structs) took "
                  << std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count()
                  << " microseconds\n";
    }

    // Read with ArgumentsReader
    {
        auto start = std::chrono::high_resolution_clock::now();

        ArgumentsReader reader(args);
        reader.beginStruct();
            int32 programIndex = reader.readInt32();
            (void) programIndex;
            reader.beginArray(); // list of paths
                while (reader.state() == Arguments::BeginStruct) {
                    reader.beginStruct();
                        int32 pathIndex = reader.readInt32();
                        (void) pathIndex;
                        reader.beginArray(); // list of waypoints
                            while (reader.state() == Arguments::BeginStruct) {
                                reader.beginStruct();
                                    double x = reader.readDouble();
                                    double y = reader.readDouble();
                                    (void) x; (void) y;

                                    reader.beginArray(); // list of waypoint attributes
                                        while (reader.state() == Arguments::BeginStruct) {
                                            reader.beginStruct();
                                                int32 attrId = reader.readInt32();
                                                double attrValue = reader.readDouble();
                                                (void) attrId; (void) attrValue;
                                            reader.endStruct();
                                        }
                                    reader.endArray(); // list of waypoint attributes
                                reader.endStruct();
                            }
                        reader.endArray(); // list of waypoints
                    reader.endStruct();
                }
            reader.endArray(); // list of paths
        reader.endStruct();

        auto stop = std::chrono::high_resolution_clock::now();
        std::cout << "ArgumentsReader took "
                  << std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count()
                  << " microseconds\n";
    }


    // Read with ArgumentsBcReader
    {
        auto start = std::chrono::high_resolution_clock::now();

        // This is exactly the same code as for ArgumentsReader except for the reader class name
        ArgumentsBcReader reader(args);
        reader.beginStruct();
            int32 programIndex = reader.readInt32();
            (void) programIndex;
            reader.beginArray(); // list of paths
                while (reader.state() == Arguments::BeginStruct) {
                    reader.beginStruct();
                        int32 pathIndex = reader.readInt32();
                        (void) pathIndex;
                        reader.beginArray(); // list of waypoints
                            while (reader.state() == Arguments::BeginStruct) {
                                reader.beginStruct();
                                    double x = reader.readDouble();
                                    double y = reader.readDouble();
                                    (void) x; (void) y;

                                    reader.beginArray(); // list of waypoint attributes
                                        while (reader.state() == Arguments::BeginStruct) {
                                            reader.beginStruct();
                                                int32 attrId = reader.readInt32();
                                                double attrValue = reader.readDouble();
                                                (void) attrId; (void) attrValue;
                                            reader.endStruct();
                                        }
                                    reader.endArray(); // list of waypoint attributes
                                reader.endStruct();
                            }
                        reader.endArray(); // list of waypoints
                    reader.endStruct();
                }
            reader.endArray(); // list of paths
        reader.endStruct();

        auto stop = std::chrono::high_resolution_clock::now();
        std::cout << "ArgumentsBcReader took "
                  << std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count()
                  << " microseconds\n";
    }

    // Read with ArgumentsBcReader (elided structs)
    // Structs are only needed where the DBus argument string format requires a single complete type,
    // but the application requires multiple types. For example, an array of pairs of ints must me a(ii).
    // But the API with beginArray() and endArray() can represent that as:
    // beginArray(), read/writeInt32(), read/writeInt32(), endArray().
    {
        auto start = std::chrono::high_resolution_clock::now();

        ArgumentsBcReader reader(args, Arguments::FerEncodeOptions::ElideStructs);
        // Same code as for ArgumentsBcReader, but with all beginStruct() and endStruct() removed and
        // looping conditions replaced. Checking state() != EndArray would work without changes, *but*
        // would cause infinite loops in error cases because state would be InvalidData and therefore
        // also != EndArray.
        int32 programIndex = reader.readInt32();
        (void) programIndex;
        reader.beginArray(); // list of paths
            while (reader.state() == Arguments::Int32) {
                int32 pathIndex = reader.readInt32();
                (void) pathIndex;
                reader.beginArray(); // list of waypoints
                    while (reader.state() == Arguments::Double) {
                        double x = reader.readDouble();
                        double y = reader.readDouble();
                        (void) x; (void) y;

                        reader.beginArray(); // list of waypoint attributes
                            while (reader.state() == Arguments::Int32) {
                                int32 attrId = reader.readInt32();
                                double attrValue = reader.readDouble();
                                (void) attrId; (void) attrValue;
                            }
                        reader.endArray(); // list of waypoint attributes
                    }
                reader.endArray(); // list of waypoints
            }
        reader.endArray(); // list of paths

        auto stop = std::chrono::high_resolution_clock::now();
        std::cout << "ArgumentsBcReader (elided structs) took "
                  << std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count()
                  << " microseconds\n";
    }

    return 0;
}
