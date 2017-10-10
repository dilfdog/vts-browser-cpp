/**
 * Copyright (c) 2017 Melown Technologies SE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * *  Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <cassert>
#include <unistd.h> // usleep
#include "Map.h"

#import <Dispatch/Dispatch.h>

std::shared_ptr<vts::Map> map;

namespace
{
	bool dataFinishing;
	dispatch_queue_t dataQueue;
	EAGLContext *glContext;
	
	void dataUpdate()
	{
		if (dataFinishing)
			return;
		
		// queue next iteration
		dispatch_async(dataQueue,
		^{
			dataUpdate();
		});
		
		// process some resources (which may require opengl context)
		[EAGLContext setCurrentContext:glContext];
		map->dataTick();
		[EAGLContext setCurrentContext:nullptr];
        usleep(50000);
	}
}

void mapInitialize()
{
	assert(!map);

	// create the map
	{
        vts::MapCreateOptions createOptions;
        createOptions.clientId = "vts-browser-ios";
        createOptions.disableCache = true;
        map = std::make_shared<vts::Map>(createOptions);
    }
    
    // configure the map for mobile use
    {
        vts::MapOptions &opt = map->options();
        opt.maxTexelToPixelScale = 3.2;
        opt.maxBalancedCoarsenessScale = 4.5;
        opt.maxResourcesMemory = 0;
        opt.tiltLimitAngleHigh = 320;
        opt.maxResourceProcessesPerTick = -1; // the resources are processed on a separate thread
    }
    
    map->callbacks().loadTexture = std::bind(&vts::renderer::loadTexture, std::placeholders::_1, std::placeholders::_2);
    map->callbacks().loadMesh = std::bind(&vts::renderer::loadMesh, std::placeholders::_1, std::placeholders::_2);
    
    // create data thread
	dataQueue = dispatch_queue_create("vts.map.data", NULL);
	dispatch_async(dataQueue,
	^{
		glContext = [[EAGLContext alloc] initWithAPI: kEAGLRenderingAPIOpenGLES3];
		[EAGLContext setCurrentContext:glContext];
	
	    map->dataInitialize(vts::Fetcher::create(vts::FetcherOptions()));
	    dataFinishing = false;
	    dataUpdate();
	});
}

void mapFinalize()
{
	if (dataQueue)
	{
		dispatch_sync(dataQueue,
		^{
			map->dataFinalize();
			dataFinishing = true;
			glContext = nullptr;
		});
		dataQueue = nullptr;
	}

	map.reset();
}

EAGLSharegroup *mapGlSharegroup()
{
	return glContext.sharegroup;
}


