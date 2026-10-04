#version 450
layout(set=0,binding=0) uniform sampler2D srcImage;
layout(set=0,binding=1) uniform sampler2D overlayImage;
layout(push_constant) uniform PC { uint quarterTurns; float srcAspect; float dstAspect; uint diagnosticMode; uint overlayEnabled; float cornerRadius; uint exifOrientation; uint cropEnabled; float cropX0; float cropY0; float cropX1; float cropY1; } pc;
layout(location=0) in vec2 uv;
layout(location=0) out vec4 outColor;

vec2 sourceUv(vec2 q) {
    if(pc.exifOrientation==2u) return vec2(1.0-q.x,q.y);
    if(pc.exifOrientation==3u) return vec2(1.0-q.x,1.0-q.y);
    if(pc.exifOrientation==4u) return vec2(q.x,1.0-q.y);
    if(pc.exifOrientation==5u) return vec2(q.y,q.x);
    if(pc.exifOrientation==6u) return vec2(q.y,1.0-q.x);
    if(pc.exifOrientation==7u) return vec2(1.0-q.y,1.0-q.x);
    if(pc.exifOrientation==8u) return vec2(1.0-q.y,q.x);
    if(pc.exifOrientation==1u) return q;
    if(pc.quarterTurns==1u) return vec2(q.y,1.0-q.x);
    if(pc.quarterTurns==2u) return vec2(1.0-q.x,1.0-q.y);
    if(pc.quarterTurns==3u) return vec2(1.0-q.y,q.x);
    return q;
}

void main(){
    if(pc.diagnosticMode == 1u){
        ivec2 c = ivec2(gl_FragCoord.xy) / 32;
        float v = ((c.x + c.y) & 1) == 0 ? 0.15 : 0.85;
        outColor = vec4(v,v,v,1.0);
        return;
    }

    vec2 q=uv;
    if(pc.dstAspect > pc.srcAspect){
        float w=pc.srcAspect/pc.dstAspect;
        q.x=(uv.x-0.5)/w+0.5;
        if(q.x<0.0||q.x>1.0){outColor=vec4(0,0,0,1);return;}
    } else {
        float h=pc.dstAspect/pc.srcAspect;
        q.y=(uv.y-0.5)/h+0.5;
        if(q.y<0.0||q.y>1.0){outColor=vec4(0,0,0,1);return;}
    }
    // Idle video-mode crop: the display spans the record window, not the full
    // source. The CPU passes the window in rotated-frame fractions so this
    // stays correct under quarter turns; srcAspect is the window aspect, so
    // the fit above is exact (no bars, no stretch). Applied after the fit,
    // in the same order as displayToSource (window -> full source).
    if(pc.cropEnabled != 0u){
        q=vec2(mix(pc.cropX0,pc.cropX1,q.x),mix(pc.cropY0,pc.cropY1,q.y));
    }
    vec2 s=sourceUv(q);

    // 100+ is an application-owned auxiliary texture (scope). The same
    // aspect-fit mapping is used so waveform grids and vectorscope circles are
    // never stretched by the Compose card rectangle.
    if(pc.diagnosticMode >= 100u){
        // The scope pixels are rendered by this Vulkan SurfaceView, underneath
        // Compose. A Compose RoundedCornerShape can draw the matching border but
        // cannot clip pixels that have already been rendered here, so apply the
        // same destination-local rounded mask natively.
        // cornerRadius is expressed as radius / destination-height. Work in
        // destination-height units so the corner remains circular for non-square cards.
        float r = clamp(pc.cornerRadius, 0.0, 0.5);
        if (r > 0.0) {
            vec2 scale = vec2(pc.dstAspect, 1.0);
            vec2 halfSize = 0.5 * scale;
            vec2 d = abs((uv - vec2(0.5)) * scale) - (halfSize - vec2(r));
            float roundedDistance = length(max(d, vec2(0.0))) + min(max(d.x, d.y), 0.0) - r;
            if (roundedDistance > 0.0) discard;
        }
        outColor = texture(srcImage, s);
        return;
    }

    if(pc.diagnosticMode == 5u){
        ivec2 cell=ivec2(floor(s*vec2(16.0,12.0)));
        float b=((cell.x+cell.y)&1)==0 ? 0.15 : 0.75;
        outColor=vec4(s.x,s.y,b,1.0);
        return;
    }

    vec4 sampled=texture(srcImage,s);
    if(pc.diagnosticMode == 4u){
        vec3 x=max(sampled.rgb,vec3(0.0));
        x=x/(vec3(1.0)+x);
        x=pow(x,vec3(1.0/2.2));
        outColor=vec4(x,1.0);
        return;
    }
    if(pc.overlayEnabled != 0u){
        vec4 o=texture(overlayImage,s);
        sampled.rgb=o.rgb+sampled.rgb*(1.0-o.a);
    }
    outColor=sampled;
}
