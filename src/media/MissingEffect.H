// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "FilterBase.H"
#include "FilterFactory.H"
#include <tinyxml.h>
#include <memory>
#include <string>
namespace nle {
// Keep unavailable plugin XML intact when editing an older project. The
// renderer refuses to silently export a project with a missing effect.
class MissingEffect : public FilterBase {
  std::string id,label;
  std::unique_ptr<TiXmlElement> xml;
public:
  explicit MissingEffect(std::string identifier):id(std::move(identifier)),label("Missing effect: "+id){m_bypass=false;m_expanded=true;}
  const char *name(){return label.c_str();}
  const char *identifier(){return id.c_str();}
  FilterData *getFilterData(){return nullptr;}
  void setFilterData(FilterData*){}
  void readXML(TiXmlElement *source){xml.reset(new TiXmlElement(*source));}
  void writeXML(TiXmlElement *destination){if(xml)*destination=*xml;}
};
class MissingEffectFactory : public FilterFactory {
  std::string id;
public:
  explicit MissingEffectFactory(std::string identifier):id(std::move(identifier)){}
  const char *name(){return id.c_str();}
  const char *identifier(){return id.c_str();}
  FilterBase *get(Clip*){return new MissingEffect(id);}
};
}
