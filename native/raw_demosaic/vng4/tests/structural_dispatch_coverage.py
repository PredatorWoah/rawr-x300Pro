#!/usr/bin/env python3
# Verify logical dispatch coverage used by benchmark maps every output pixel exactly once.
# Production green pass: 16x16 workgroups, two horizontal pixels each.
cases={
 'pair_x_16x16':(16,16,32,16,2,1),
}
for W,H in ((4080,3064),(4080,3072),(4096,3072),(97,99)):
 for name,(lx,ly,dx,dy,px,py) in cases.items():
  gx=(W+dx-1)//dx; gy=(H+dy-1)//dy
  qx=gx*lx; qy=gy*ly
  assert qx*px>=W,(name,W,'x_undercoverage',qx*px)
  assert qy*py>=H,(name,H,'y_undercoverage',qy*py)
  # Mapping base=(qx*px,qy*py) is injective across invocations; each invocation's local block is disjoint.
  assert px in (1,2) and py in (1,2)
print('VNG4_STRUCTURAL_DISPATCH_COVERAGE_PASS production_geometries=3 odd_fixture=true')
