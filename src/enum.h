#ifndef ENUM_H
#define ENUM_H

enum DNode_type
{
  MNM_TYPE_ORIGIN,
  MNM_TYPE_DEST,
  MNM_TYPE_FWJ,
  MNM_TYPE_GRJ
};
enum DLink_type
{
  MNM_TYPE_CTM,
  MNM_TYPE_PQ,
  MNM_TYPE_LQ,
  MNM_TYPE_LTM
};
enum Dta_type
{
  MNM_TYPE_RANDOM,
  MNM_TYPE_BOSTON,
  MNM_TYPE_HYBRID
};
enum Vehicle_type
{
  MNM_TYPE_ADAPTIVE,
  MNM_TYPE_STATIC
};
enum Record_type
{
  MNM_TYPE_LRN
};

enum DNode_type_multiclass
{
  MNM_TYPE_ORIGIN_MULTICLASS,
  MNM_TYPE_DEST_MULTICLASS,
  MNM_TYPE_FWJ_MULTICLASS
};
enum DLink_type_multiclass
{
  MNM_TYPE_CTM_MULTICLASS,
  MNM_TYPE_LQ_MULTICLASS,
  MNM_TYPE_PQ_MULTICLASS
};

#endif