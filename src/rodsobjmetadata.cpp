/**
 * @file rodsobjmetadata.cpp
 * @brief Implementation of Kanki library class RodsObjMetadata
 *
 * The Kanki class RodsObjMetadata implements a container for iRODS
 * object AVU metadata and interfaces for metadata updates.
 *
 * Copyright (C) 2014-2015 University of Jyväskylä. All rights reserved.
 * License: The BSD 3-Clause License, see LICENSE file for details.
 *
 * @author Ilari Korhonen
 */

// Kanki library class RodsObjMetadata header
#include "rodsobjmetadata.h"

namespace Kanki {

const char *RodsObjMetadata::AddOperation = "add";
const char *RodsObjMetadata::ModOperation = "mod";
const char *RodsObjMetadata::RmOperation = "rm";

RodsObjMetadata::RodsObjMetadata(Kanki::RodsConnection *theConn, RodsObjEntryPtr theObjEntry)
{
    this->conn = theConn;
    this->objEntry = theObjEntry;
}

int RodsObjMetadata::refresh()
{
    Kanki::RodsGenQuery metaQuery(this->conn);
    int status = 0;

    // if the object in question is a data object
    if (this->objEntry->objType == DATA_OBJ_T)
    {
        metaQuery.addQueryAttribute(COL_META_DATA_ATTR_NAME);
        metaQuery.addQueryAttribute(COL_META_DATA_ATTR_VALUE);
        metaQuery.addQueryAttribute(COL_META_DATA_ATTR_UNITS);
    }

    else if (this->objEntry->objType == COLL_OBJ_T)
    {
        metaQuery.addQueryAttribute(COL_META_COLL_ATTR_NAME);
        metaQuery.addQueryAttribute(COL_META_COLL_ATTR_VALUE);
        metaQuery.addQueryAttribute(COL_META_COLL_ATTR_UNITS);
    }

    // set query condition, object name
    metaQuery.addQueryCondition(this->objEntry->objType == DATA_OBJ_T ? COL_DATA_NAME : COL_COLL_NAME,
                                Kanki::RodsGenQuery::isEqual, this->objEntry->objName);

    // if we are querying a data object, specify collection path
    if (this->objEntry->objType == DATA_OBJ_T)
        metaQuery.addQueryCondition(COL_COLL_NAME, Kanki::RodsGenQuery::isEqual, this->objEntry->collPath);

    // execute the genquery for object metadata
    if ((status = metaQuery.execute()) < 0)
    {
        // in the case of a failure, return error code from rods api to caller
        return (status);
    }

    // otherwise we update our internal metadata structures
    else {
        // clear hashtables
        this->attrValues.clear();
        this->attrUnits.clear();

        // retrieve results from the genquery
        std::vector<std::string> names = metaQuery.getResultSet(0);
        std::vector<std::string> values = metaQuery.getResultSet(1);
        std::vector<std::string> units = metaQuery.getResultSet(2);

        // add the relational result set into internal structures
        for (unsigned int i = 0; i < names.size() && i < values.size() && i < units.size(); i++)
            this->addToStore(names.at(i), values.at(i), units.at(i));
    }

    // the metadata refresh was successful
    return (0);
}

void RodsObjMetadata::addToStore(std::string name, std::string value, std::string unit)
{
    this->addToKeyVals(&this->attrValues, name, value);
    this->addToKeyVals(&this->attrUnits, name, unit);
}

void RodsObjMetadata::addToKeyVals(RodsObjMetadata::KeyVals *keyValsPtr, std::string key, std::string value)
{
    // if there is no value vector for this key in the hastable
    if (keyValsPtr->find(key) == keyValsPtr->end())
    {
        // initialize hashtable entry with an empty vector object
        (*keyValsPtr)[key] = std::vector<std::string>();
    }

    // the value is pushed to the end of the value vector
    keyValsPtr->at(key).push_back(value);
}

void RodsObjMetadata::removeFromKeyVals(RodsObjMetadata::KeyVals *keyValsPtr, std::string key, std::string value)
{
    // try to find value vector
    if (keyValsPtr->find(key) != keyValsPtr->end())
    {
        // get reference to value store vector
        std::vector<std::string> &values = keyValsPtr->at(key);

        // iterate thru value store vector to find instance
        for (std::vector<std::string>::iterator i = values.begin(); i != values.end(); i++)
        {
            // if found, erase instance from the vector
            if (!value.compare(*i))
                values.erase(i);
        }
    }
}

const RodsObjMetadata::KeyVals& RodsObjMetadata::getValues() const
{
    // return a constant reference of the hashtable
    return (this->attrValues);
}

const RodsObjMetadata::KeyVals& RodsObjMetadata::getUnits() const
{
    // return a constant reference of the hashtable
    return (this->attrUnits);
}

int RodsObjMetadata::addAttribute(std::string attrName, std::string attrValue, std::string attrUnit)
{
    modAVUMetadataInp_t mod;
    std::string objPath;
    int status = 0;

    // get object full path
    objPath = this->objEntry->getObjectFullPath();

    // initialize iRODS API metadata modification struct
    mod.arg0 = (char*)RodsObjMetadata::AddOperation;
    mod.arg1 = (char*)(this->objEntry->objType == DATA_OBJ_T ? RodsObjEntry::DataObjType : RodsObjEntry::CollObjType);
    mod.arg2 = (char*)objPath.c_str();
    mod.arg3 = (char*)attrName.c_str();
    mod.arg4 = (char*)attrValue.c_str();
    mod.arg5 = (char*)attrUnit.c_str();
    mod.arg6 = "";
    mod.arg7 = "";
    mod.arg8 = "";
    mod.arg9 = "";

    // execute metadata add thru rods api
    if ((status = rcModAVUMetadata(this->conn->commPtr(), &mod)) < 0)
    {
        // return error code to caller
        return (status);
    }

    // update internal structures
    else {
        this->addToStore(attrName, attrValue, attrUnit);
    }

    // otherwise successful
    return (status);
}

int RodsObjMetadata::modifyAttribute(std::string attrName, std::string valueStr, std::string newValueStr,
                                     std::string unitStr, std::string newUnitStr)
{
    modAVUMetadataInp_t mod;
    std::string objPath, valueArg, unitArg;
    int status = 0;

    // get object full path
    objPath = this->objEntry->getObjectFullPath();

    // construct value modify argument string
    valueArg = "v:" + newValueStr;

    // if the update is being modified as well
    if (unitStr.length() > 0)
    {
        unitArg = "u:" + newUnitStr;
    }

    // initialize iRODS API metadata modification struct
    mod.arg0 = (char*)RodsObjMetadata::ModOperation;
    mod.arg1 = (char*)(this->objEntry->objType == DATA_OBJ_T ? RodsObjEntry::DataObjType : RodsObjEntry::CollObjType);
    mod.arg2 = (char*)objPath.c_str();
    mod.arg3 = (char*)attrName.c_str();
    mod.arg4 = (char*)valueStr.c_str();

    // set arguments for unit modify if needed
    if (unitStr.length() > 0)
    {
        mod.arg5 = (char*)unitStr.c_str();
        mod.arg6 = (char*)valueArg.c_str();
        mod.arg7 = (char*)unitArg.c_str();
        mod.arg8 = "";
        mod.arg9 = "";
    }

    // otherwise just the value is modified
    else {
        mod.arg5 = (char*)valueArg.c_str();
        mod.arg6 = "";
        mod.arg7 = "";
        mod.arg8 = "";
        mod.arg9 = "";
    }

    // execute metadata update thru rods api
    if ((status = rcModAVUMetadata(this->conn->commPtr(), &mod)) < 0)
    {
        // in the case of failure, return error code
        return (status);
    }

    // update internal data structures accordingly
    else {
        // take references to value and unit vectors
        std::vector<std::string> &values = this->attrValues[attrName];
        std::vector<std::string> &units = this->attrUnits[attrName];

        // find the correct AVU triplet from the vectors
        for (unsigned int i = 0; i < values.size() && i < units.size(); i++)
        {
            // if a match was found, value equals and if unit is defined it equals
            if (!values.at(i).compare(valueStr) && units.at(i).length() && !units.at(i).compare(unitStr))
            {
                // modify attribute
                values.at(i) = newValueStr;
                units.at(i) = newUnitStr;

                // and we are done
                return (status);
            }
        }

        // in the case we are corrupt, rectify situation
        values.push_back(newValueStr);
        units.push_back(newUnitStr);
    }

    // we were successful
    return (status);
}

int RodsObjMetadata::removeAttribute(std::string attrName, std::string valueStr, std::string unitStr)
{
    modAVUMetadataInp_t remove;
    std::string objPath;
    int status = 0;

    // get object path
    objPath = this->objEntry->getObjectFullPath();

    // initialize iRODS API metadata modification struct
    remove.arg0 = (char*)RodsObjMetadata::RmOperation;
    remove.arg1 = (char*)(this->objEntry->objType == DATA_OBJ_T ? RodsObjEntry::DataObjType : RodsObjEntry::CollObjType);
    remove.arg2 = (char*)objPath.c_str();
    remove.arg3 = (char*)attrName.c_str();
    remove.arg4 = (char*)valueStr.c_str();
    remove.arg5 = (char*)unitStr.c_str();
    remove.arg6 = "";
    remove.arg7 = "";
    remove.arg8 = "";
    remove.arg9 = "";

    // execute metadata removal thru rods api
    if ((status = rcModAVUMetadata(this->conn->commPtr(), &remove)) < 0)
    {
        return (status);
    }

    // update internal data structures
    else {
        // take references to value and unit vectors
        std::vector<std::string> &values = this->attrValues[attrName];
        std::vector<std::string> &units = this->attrUnits[attrName];

        // find the correct AVU triplet from the vectors
        for (unsigned int i = 0; i < values.size() && i < units.size(); i++)
        {
            // if a match was found, value equals and if unit is defined it equals
            if (!values.at(i).compare(valueStr) && units.at(i).length() && !units.at(i).compare(unitStr))
            {
                // get iterators for entries to be erased and erase
                std::vector<std::string>::iterator value = values.begin() + i;
                std::vector<std::string>::iterator unit = units.begin() + i;

                values.erase(value);
                units.erase(unit);
            }
        }
    }

    return (status);
}

} // namespace Kanki
