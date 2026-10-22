#include "Frei0r/Frei0rModule.H"

#include <dlfcn.h>
#include "NodeFilterFrei0rFactoryPlugin.H"
#include "Frei0rNode.H"
#include <iostream>
#include <cstring>


NodeFilterFrei0rFactoryPlugin::NodeFilterFrei0rFactoryPlugin( std::string filename )
{
    m_handle=nle::acquire_frei0r(filename,m_info);
    m_ok=m_handle!=nullptr;
}

NodeFilterFrei0rFactoryPlugin::~NodeFilterFrei0rFactoryPlugin() {nle::release_frei0r(m_handle);}

Frei0rNode* NodeFilterFrei0rFactoryPlugin::get( int w, int h )
{
	Frei0rNode* effect = new Frei0rNode( &m_info, m_handle, w, h );
	return effect;

}
const char* NodeFilterFrei0rFactoryPlugin::name()
{
	return m_info.name;
}
const char* NodeFilterFrei0rFactoryPlugin::identifier()
{
	static char buffer[255];
	std::string result = "effect:frei0r:";
	result += name();
	strcpy( buffer, result.c_str() );
	return buffer;
}
