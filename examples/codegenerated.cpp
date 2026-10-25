// SPDX-FileCopyrightText: 2026 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#include "arguments.h"
#include "argumentsbcreader.h"
#include "argumentswriter.h"
#include "error.h"

#include "codegenerated_reader.h"

#include <chrono>
#include <iostream>

struct MachineProgramReceiver
{
    void processArg0(int32 arg) { m_sum += arg; } // program index
    void processArg1(int32 arg) { m_sum += arg; } // path index
    void processArg2(double arg) { m_sum += static_cast<int32>(arg); } // x coordinate
    void processArg3(double arg) { m_sum += static_cast<int32>(arg); } // y coordinate
    void processArg4(int32 arg) { m_sum += arg; } // attribute ID
    void processArg5(double arg) { m_sum += arg; } // attribute value

    // Write the received data somewhere to prevent the compiler from optimizing out *everything*
    int32 m_sum = 0;
};

int main(int, char *[])
{
    // Demo and benchmark with a signature taken from an existing machine control application.
    // It's about a processing program made of tool paths, made of 2D waypoints, with an optional list of
    // attributes at each waypoint.

    // Write Arguments - real-world type signature "(ia(ia(dda(id))))" - copied from bytecoded.cpp
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

    // First a copy of the fastest test from bytecoded.cpp, for comparison
    {
        auto start = std::chrono::high_resolution_clock::now();

        ArgumentsBcReader reader(args, Arguments::FerEncodeOptions::ElideStructs);
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

    // Read with MachineProgramReader (a code-generated class) and our MachineProgramReceiver
    {
        auto start = std::chrono::high_resolution_clock::now();

        MachineProgramReader<MachineProgramReceiver> reader(args);
        reader.readAll();

        auto stop = std::chrono::high_resolution_clock::now();
        std::cout << "MachineProgramReader took "
                  << std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count()
                  << " microseconds"
                  // now show the compiler that we "use" the value from MachineProgramReceiver
                  << ((reader.m_sum & 1) ? "" : " ")
                  << '\n';
    }

    return 0;
}
