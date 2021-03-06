//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2013, John Haddon. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are
//  met:
//
//      * Redistributions of source code must retain the above
//        copyright notice, this list of conditions and the following
//        disclaimer.
//
//      * Redistributions in binary form must reproduce the above
//        copyright notice, this list of conditions and the following
//        disclaimer in the documentation and/or other materials provided with
//        the distribution.
//
//      * Neither the name of John Haddon nor the names of
//        any other contributors to this software may be used to endorse or
//        promote products derived from this software without specific prior
//        written permission.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
//  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
//  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
//  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
//  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
//  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
//  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
//  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
//  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
//  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
//  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//////////////////////////////////////////////////////////////////////////

#include "IECore/Primitive.h"

#include "GafferScene/ResamplePrimitiveVariables.h"

#include "GafferOSL/OSLShader.h"
#include "GafferOSL/OSLObject.h"
#include "GafferOSL/ShadingEngine.h"

using namespace Imath;
using namespace IECore;
using namespace Gaffer;
using namespace GafferScene;
using namespace GafferOSL;

IE_CORE_DEFINERUNTIMETYPED( OSLObject );

size_t OSLObject::g_firstPlugIndex;

namespace
{

CompoundDataPtr prepareShadingPoints( const Primitive *primitive )
{
	CompoundDataPtr shadingPoints = new CompoundData;
	for( PrimitiveVariableMap::const_iterator it = primitive->variables.begin(), eIt = primitive->variables.end(); it != eIt; ++it )
	{
		// cast is ok - we're only using it to be able to reference the data from the shadingPoints,
		// but nothing will modify the data itself.
		shadingPoints->writable()[it->first] = boost::const_pointer_cast<Data>( it->second.data );
	}

	return shadingPoints;
}

};

OSLObject::OSLObject( const std::string &name )
	:	SceneElementProcessor( name, Filter::NoMatch )
{
	storeIndexOfNextChild( g_firstPlugIndex );
	addChild( new ShaderPlug( "shader" ) );
	addChild( new IntPlug( "interpolation", Plug::In, PrimitiveVariable::Vertex, PrimitiveVariable::Invalid, PrimitiveVariable::FaceVarying ) );
	addChild( new ScenePlug( "__resampledIn", Plug::In, Plug::Default & ~Plug::Serialisable ) );

	addChild( new StringPlug( "__resampleNames", Plug::Out ) );

	GafferScene::ResamplePrimitiveVariablesPtr resample = new ResamplePrimitiveVariables( "__resample" );
	addChild( resample );

	//todo only resample variables which we've read from in the OSL shader
	resample->namesPlug()->setInput( resampledNamesPlug() );
	resample->inPlug()->setInput( inPlug() );
	resample->interpolationPlug()->setInput( interpolationPlug() );
	resample->filterPlug()->setInput( filterPlug() );

	resampledInPlug()->setInput( resample->outPlug() );

	// Pass-throughs for things we don't want to modify
	outPlug()->attributesPlug()->setInput( inPlug()->attributesPlug() );
	outPlug()->transformPlug()->setInput( inPlug()->transformPlug() );
}

OSLObject::~OSLObject()
{
}

GafferScene::ShaderPlug *OSLObject::shaderPlug()
{
	return getChild<ShaderPlug>( g_firstPlugIndex );
}

const GafferScene::ShaderPlug *OSLObject::shaderPlug() const
{
	return getChild<ShaderPlug>( g_firstPlugIndex );
}

Gaffer::IntPlug *OSLObject::interpolationPlug()
{
	return getChild<IntPlug>( g_firstPlugIndex + 1 );
}

const Gaffer::IntPlug *OSLObject::interpolationPlug() const
{
	return getChild<IntPlug>( g_firstPlugIndex + 1 );
}

ScenePlug *OSLObject::resampledInPlug()
{
	return getChild<ScenePlug>( g_firstPlugIndex + 2 );
}

const ScenePlug *OSLObject::resampledInPlug() const
{
	return getChild<ScenePlug>( g_firstPlugIndex + 2 );
}

StringPlug *OSLObject::resampledNamesPlug()
{
	return getChild<StringPlug>( g_firstPlugIndex + 3 );
}

const StringPlug *OSLObject::resampledNamesPlug() const
{
	return getChild<StringPlug>( g_firstPlugIndex + 3 );
}

void OSLObject::affects( const Gaffer::Plug *input, AffectedPlugsContainer &outputs ) const
{
	SceneElementProcessor::affects( input, outputs );

	if( input == shaderPlug() ||
		input == inPlug()->transformPlug() ||
		input == interpolationPlug() ||
		input == resampledInPlug()->objectPlug()
		)
	{
		outputs.push_back( outPlug()->objectPlug() );
	}
	else if( input == inPlug()->objectPlug() )
	{
		outputs.push_back( resampledNamesPlug() );
	}
	else if( input == outPlug()->objectPlug() )
	{
		outputs.push_back( outPlug()->boundPlug() );
	}
}

bool OSLObject::acceptsInput( const Gaffer::Plug *plug, const Gaffer::Plug *inputPlug ) const
{
	if( !SceneElementProcessor::acceptsInput( plug, inputPlug ) )
	{
		return false;
	}

	if( !inputPlug )
	{
		return true;
	}

	if( plug == shaderPlug() )
	{
		if( const GafferScene::Shader *shader = runTimeCast<const GafferScene::Shader>( inputPlug->source<Plug>()->node() ) )
		{
			const OSLShader *oslShader = runTimeCast<const OSLShader>( shader );
			return oslShader && oslShader->typePlug()->getValue() == "osl:surface";
		}
	}

	return true;
}

bool OSLObject::processesBound() const
{
	return runTimeCast<const OSLShader>( shaderPlug()->source<Plug>()->node() );
}

void OSLObject::hashProcessedBound( const ScenePath &path, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	hashProcessedObject( path, context, h );
}

Imath::Box3f OSLObject::computeProcessedBound( const ScenePath &path, const Gaffer::Context *context, const Imath::Box3f &inputBound ) const
{
	ConstObjectPtr object = outPlug()->objectPlug()->getValue();
	if( const Primitive *primitive = runTimeCast<const Primitive>( object.get() ) )
	{
		return primitive->bound();
	}
	return inputBound;
}

bool OSLObject::processesObject() const
{
	return runTimeCast<const OSLShader>( shaderPlug()->source<Plug>()->node() );
}

void OSLObject::hashProcessedObject( const ScenePath &path, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	const OSLShader *shader = runTimeCast<const OSLShader>( shaderPlug()->source<Plug>()->node() );
	if( shader )
	{
		shader->attributesHash( h );
	}

	interpolationPlug()->hash( h );
	h.append( inPlug()->fullTransformHash( path ) );
	h.append( resampledInPlug()->objectPlug()->hash() );
}

static const IECore::InternedString g_world("world");

IECore::ConstObjectPtr OSLObject::computeProcessedObject( const ScenePath &path, const Gaffer::Context *context, IECore::ConstObjectPtr inputObject ) const
{
	const Primitive *inputPrimitive = runTimeCast<const Primitive>( inputObject.get() );
	if( !inputPrimitive )
	{
		return inputObject;
	}

	ConstOSLShaderPtr shader = runTimeCast<const OSLShader>( shaderPlug()->source<Plug>()->node() );
	ConstShadingEnginePtr shadingEngine = shader ? shader->shadingEngine() : NULL;

	if( !shadingEngine )
	{
		return inputObject;
	}

	PrimitiveVariable::Interpolation interpolation = static_cast<PrimitiveVariable::Interpolation>( interpolationPlug()->getValue() );

	IECore::ConstPrimitivePtr resampledObject = IECore::runTimeCast<const IECore::Primitive>( resampledInPlug()->objectPlug()->getValue() );
	CompoundDataPtr shadingPoints = prepareShadingPoints( resampledObject.get() );

	PrimitivePtr outputPrimitive = inputPrimitive->copy();

	ShadingEngine::Transforms transforms;

	transforms[ g_world ] = ShadingEngine::Transform( inPlug()->fullTransform( path ));

	CompoundDataPtr shadedPoints = shadingEngine->shade( shadingPoints.get(), transforms );
	for( CompoundDataMap::const_iterator it = shadedPoints->readable().begin(), eIt = shadedPoints->readable().end(); it != eIt; ++it )
	{

		// Ignore the output color closure as the debug closures are used to define what is 'exported' from the shader
		if( it->first != "Ci" )
		{
			outputPrimitive->variables[it->first] = PrimitiveVariable( interpolation, it->second );
		}
	}

	return outputPrimitive;
}

void OSLObject::hash( const ValuePlug *output, const Context *context, IECore::MurmurHash &h ) const
{
	SceneElementProcessor::hash( output, context, h );

	if( output == resampledNamesPlug() )
	{
		inPlug()->objectPlug()->hash( h );
	}
}

void OSLObject::compute( Gaffer::ValuePlug *output, const Gaffer::Context *context ) const
{
	if( output == resampledNamesPlug() )
	{
		ConstPrimitivePtr prim = runTimeCast<const IECore::Primitive>( inPlug()->objectPlug()->getValue() );

		if (!prim)
		{
			static_cast<StringPlug *>( output )->setToDefault();
			return;
		}

		std::string nonConstantPrimitiveVariables;

		for( PrimitiveVariableMap::const_iterator it = prim->variables.begin(); it != prim->variables.end(); ++it )
		{
			if( it->second.interpolation == PrimitiveVariable::Constant )
			{
				continue;
			}

			nonConstantPrimitiveVariables += " " + it->first;
		}

		static_cast<StringPlug *>( output )->setValue( nonConstantPrimitiveVariables );
		return;
	}

	SceneElementProcessor::compute( output, context );
}
