///////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2012-2013 DreamWorks Animation LLC
//
// All rights reserved. This software is distributed under the
// Mozilla Public License 2.0 ( http://www.mozilla.org/MPL/2.0/ )
//
// Redistributions of source code must retain the above copyright
// and license notice and the following restrictions and disclaimer.
//
// *     Neither the name of DreamWorks Animation nor the names of
// its contributors may be used to endorse or promote products derived
// from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
// IN NO EVENT SHALL THE COPYRIGHT HOLDERS' AND CONTRIBUTORS' AGGREGATE
// LIABILITY FOR ALL CLAIMS REGARDLESS OF THEIR BASIS EXCEED US$250.00.
//
///////////////////////////////////////////////////////////////////////////
//
/// @file SOP_OpenVDB_Analysis.cc
///
/// @author FX R&D OpenVDB team
///
/// @brief Compute gradient fields and other differential properties from VDB volumes

#include <houdini_utils/ParmFactory.h>
#include <openvdb_houdini/Utils.h>
#include <openvdb_houdini/SOP_NodeVDB.h>
#include <openvdb/tools/GridOperators.h>
#include <UT/UT_Interrupt.h>
#if (UT_VERSION_INT >= 0x0c050157) // 12.5.343 or later
#include <GEO/GEO_PrimVDB.h> // for GEOvdbProcessTypedGridScalar(), etc.
#endif

namespace cvdb = openvdb;
namespace hvdb = openvdb_houdini;
namespace hutil = houdini_utils;


class SOP_OpenVDB_Analysis: public hvdb::SOP_NodeVDB
{
public:
    SOP_OpenVDB_Analysis(OP_Network*, const char* name, OP_Operator*);
    virtual ~SOP_OpenVDB_Analysis() {}

    static OP_Node* factory(OP_Network*, const char* name, OP_Operator*);

    static const char* sOpName[];

    enum OpId {
        OP_GRADIENT   = 0,
        OP_CURVATURE  = 1,
        OP_LAPLACIAN  = 2,
        OP_CPT        = 3,
        OP_DIVERGENCE = 4,
        OP_CURL       = 5,
        OP_MAGNITUDE  = 6,
        OP_NORMALIZE  = 7
    };

protected:
    virtual OP_ERROR cookMySop(OP_Context&);
    virtual unsigned disableParms();
};


////////////////////////////////////////


const char* SOP_OpenVDB_Analysis::sOpName[] = {
    "gradient",
    "curvature",
    "laplacian",
    "closest point transform",
    "divergence",
    "curl",
    "magnitude",
    "normalize"
};


////////////////////////////////////////


void
newSopOperator(OP_OperatorTable* table)
{
    if (table == NULL) return;

    hutil::ParmList parms;

    // Group pattern
    parms.add(hutil::ParmFactory(PRM_STRING, "group", "Group")
        .setHelpText("Specify a subset of the input VDB grids to be processed.")
        .setChoiceList(&hutil::PrimGroupMenu));

    // Operator
    {
        const char* items[] = {
            "gradient",     "Gradient       (Scalar->Vector)",
            "curvature",    "Curvature     (Scalar->Scalar)",
            "laplacian",    "Laplacian      (Scalar->Scalar)",
            "closestpoint", "Closest Point (Scalar->Vector)",
            "divergence",   "Divergence    (Vector->Scalar)",
            "curl",         "Curl             (Vector->Vector)",
            "length",       "Length         (Vector->Scalar)",
            "normalize",    "Normalize     (Vector->Vector)",
            NULL
        };
        parms.add(hutil::ParmFactory(PRM_ORD, "operator", "Operator")
            .setDefault(PRMzeroDefaults)
            .setChoiceListItems(PRM_CHOICELIST_SINGLE, items));
    }


    { // Output name
        const char* items[] = {
            "keep",     "Keep Incoming VDB Names",
            "append",   "Append Operation Name",
            "custom",   "Custom Name",
            NULL
        };
        parms.add(hutil::ParmFactory(PRM_ORD, "outputName", "Output Name")
            .setDefault(PRMzeroDefaults)
            .setHelpText("Rename output grid(s)")
            .setChoiceListItems(PRM_CHOICELIST_SINGLE, items));
    }

    parms.add(hutil::ParmFactory(PRM_STRING, "customName", "Custom Name")
        .setHelpText("Renames all output grids with this custom name"));


    // Obsolete parameters
    hutil::ParmList obsoleteParms;
    obsoleteParms.add(hutil::ParmFactory(PRM_TOGGLE, "threaded", "Multithreaded"));

    // Register this operator.
    hvdb::OpenVDBOpFactory("OpenVDB Analysis", SOP_OpenVDB_Analysis::factory, parms, *table)
        .setObsoleteParms(obsoleteParms)
        .addInput("VDBs to Analyze");
}


////////////////////////////////////////


OP_Node*
SOP_OpenVDB_Analysis::factory(OP_Network* net, const char* name, OP_Operator* op)
{
    return new SOP_OpenVDB_Analysis(net, name, op);
}


SOP_OpenVDB_Analysis::SOP_OpenVDB_Analysis(OP_Network* net,
    const char* name, OP_Operator* op):
    hvdb::SOP_NodeVDB(net, name, op)
{
}


////////////////////////////////////////


namespace {

template<template<typename GridT, typename InterruptT> class ToolT>
struct ToolOp
{
    ToolOp(bool t, hvdb::Interrupter& boss): mThreaded(t), mBoss(boss) {}

    template<typename GridType>
    void operator()(const GridType& inGrid)
    {
        ToolT<GridType, hvdb::Interrupter> tool(inGrid, &mBoss);
        mOutGrid = tool.process(mThreaded);
        //mOutGrid->setTransform(inGrid.transform().copy());
    }

    hvdb::GridPtr      mOutGrid;
    bool               mThreaded;
    hvdb::Interrupter& mBoss;
};

} // unnamed namespace


////////////////////////////////////////


// Enable or disable parameters in the UI.
unsigned
SOP_OpenVDB_Analysis::disableParms()
{
    unsigned changed = 0;

    bool useCustomName = evalInt("outputName", 0, 0) == 2;

    changed += enableParm("customName", useCustomName);
#ifndef SESI_OPENVDB
    setVisibleState("customName", useCustomName);
#endif

    return changed;
}


////////////////////////////////////////


OP_ERROR
SOP_OpenVDB_Analysis::cookMySop(OP_Context& context)
{
    try {
        hutil::ScopedInputLock lock(*this, context);

        const fpreal time = context.getTime();

        // This does a shallow copy of VDB-grids and deep copy of native Houdini primitives.
        duplicateSource(0, context);

        // Get the group of grids to be transformed.
        UT_String groupStr;
        evalString(groupStr, "group", 0, time);
        const GA_PrimitiveGroup* group = matchGroup(*gdp, groupStr.toStdString());

        const int whichOp = evalInt("operator", 0, time);
        if (whichOp < 0 || whichOp > 7) {
            std::ostringstream ostr;
            ostr << "expected 0 <= operator <= 7, got " << whichOp;
            throw std::runtime_error(ostr.str().c_str());
        }

        const bool threaded = true;

        hvdb::Interrupter boss(
            (std::string("Computing ") + sOpName[whichOp] + " of VDB grids").c_str());

        // For each VDB primitive (with a non-null grid pointer) in the given group...
        std::string operationName;
        for (hvdb::VdbPrimIterator it(gdp, group); it; ++it) {
            if (boss.wasInterrupted()) throw std::runtime_error("was interrupted");

            GU_PrimVDB* vdb = *it;

            hvdb::GridPtr outGrid;
            bool ok = true;
            switch (whichOp)
            {
                case OP_GRADIENT: // gradient of scalar field
                {
                    ToolOp<cvdb::tools::Gradient> op(threaded, boss);
                    ok = GEOvdbProcessTypedGridScalar(*vdb, op, /*makeUnique=*/false);
                    if (ok) outGrid = op.mOutGrid;
                    operationName = "_gradient";
                    break;
                }
                case OP_CURVATURE: // mean curvature of scalar field
                {
                    ToolOp<cvdb::tools::MeanCurvature> op(threaded, boss);
                    ok = GEOvdbProcessTypedGridScalar(*vdb, op, /*makeUnique=*/false);
                    if (ok) outGrid = op.mOutGrid;
                    operationName = "_curvature";
                    break;
                }
                case OP_LAPLACIAN: // Laplacian of scalar field
                {
                    ToolOp<cvdb::tools::Laplacian> op(threaded, boss);
                    ok = GEOvdbProcessTypedGridScalar(*vdb, op, /*makeUnique=*/false);
                    if (ok) outGrid = op.mOutGrid;
                    operationName = "_laplacian";
                    break;
                }
                case OP_CPT: // closest point transform of scalar level set
                {
                    ToolOp<cvdb::tools::Cpt> op(threaded, boss);
                    ok = GEOvdbProcessTypedGridScalar(*vdb, op, /*makeUnique=*/false);
                    if (ok) outGrid = op.mOutGrid;
                    operationName = "_cpt";
                    break;
                }
                case OP_DIVERGENCE: // divergence of vector field
                {
                    ToolOp<cvdb::tools::Divergence> op(threaded, boss);
                    ok = GEOvdbProcessTypedGridVec3(*vdb, op, /*makeUnique=*/false);
                    if (ok) outGrid = op.mOutGrid;
                    operationName = "_divergence";
                    break;
                }
                case OP_CURL: // curl (rotation) of vector field
                {
                    ToolOp<cvdb::tools::Curl> op(threaded, boss);
                    ok = GEOvdbProcessTypedGridVec3(*vdb, op, /*makeUnique=*/false);
                    if (ok) outGrid = op.mOutGrid;
                    operationName = "_curl";
                    break;
                }
                case OP_MAGNITUDE: // magnitude of vector field
                {
                    ToolOp<cvdb::tools::Magnitude> op(threaded, boss);
                    ok = GEOvdbProcessTypedGridVec3(*vdb, op, /*makeUnique=*/false);
                    if (ok) outGrid = op.mOutGrid;
                    operationName = "_magnitude";
                    break;
                }
                case OP_NORMALIZE: // normalize vector field
                {
                    ToolOp<cvdb::tools::Normalize> op(threaded, boss);
                    ok = GEOvdbProcessTypedGridVec3(*vdb, op, /*makeUnique=*/false);
                    if (ok) outGrid = op.mOutGrid;
                    operationName = "_normalize";
                    break;
                }
            }

            if (!ok) {
                UT_String inGridName = it.getPrimitiveNameOrIndex();
                std::ostringstream ss;
                ss << "Can't compute " << sOpName[whichOp] << " from grid";
                if (inGridName.isstring()) ss << " " << inGridName;
                ss << " of type " << vdb->getGrid().valueType();
                addWarning(SOP_MESSAGE, ss.str().c_str());
            }

            // Rename grid
            std::string gridName = vdb->getGridName();
            const int renaming = evalInt("outputName", 0, time);
            if (renaming == 1) {
                if (operationName.size() > 0) gridName += operationName;
            } else if (renaming == 2) {
                UT_String customName;
                evalString(customName, "customName", 0, time);
                if (customName.length() > 0) gridName = customName.toStdString();
            }

            // Replace the original VDB primitive with a new primitive that contains
            // the output grid and has the same attributes and group membership.
            hvdb::replaceVdbPrimitive(*gdp, outGrid, *vdb, true, gridName.c_str());
        }
    } catch (std::exception& e) {
        addError(SOP_MESSAGE, e.what());
    }
    return error();
}

// Copyright (c) 2012-2013 DreamWorks Animation LLC
// All rights reserved. This software is distributed under the
// Mozilla Public License 2.0 ( http://www.mozilla.org/MPL/2.0/ )
