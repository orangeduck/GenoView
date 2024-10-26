import numpy as np
import pymel.core as pm
import struct

def quat_from_xform(ts, eps=1e-10):
    qs = np.empty_like(ts[..., :1, 0].repeat(4, axis=-1))

    t = ts[..., 0, 0] + ts[..., 1, 1] + ts[..., 2, 2]

    s = 0.5 / np.sqrt(np.maximum(t + 1, eps))
    qs = np.where((t > 0)[..., np.newaxis].repeat(4, axis=-1), np.concatenate([
        (0.25 / s)[..., np.newaxis],
        (s * (ts[..., 2, 1] - ts[..., 1, 2]))[..., np.newaxis],
        (s * (ts[..., 0, 2] - ts[..., 2, 0]))[..., np.newaxis],
        (s * (ts[..., 1, 0] - ts[..., 0, 1]))[..., np.newaxis]
    ], axis=-1), qs)

    c0 = (ts[..., 0, 0] > ts[..., 1, 1]) & (ts[..., 0, 0] > ts[..., 2, 2])
    s0 = 2.0 * np.sqrt(np.maximum(1.0 + ts[..., 0, 0] - ts[..., 1, 1] - ts[..., 2, 2], eps))
    qs = np.where(((t <= 0) & c0)[..., np.newaxis].repeat(4, axis=-1), np.concatenate([
        ((ts[..., 2, 1] - ts[..., 1, 2]) / s0)[..., np.newaxis],
        (s0 * 0.25)[..., np.newaxis],
        ((ts[..., 0, 1] + ts[..., 1, 0]) / s0)[..., np.newaxis],
        ((ts[..., 0, 2] + ts[..., 2, 0]) / s0)[..., np.newaxis]
    ], axis=-1), qs)

    c1 = (~c0) & (ts[..., 1, 1] > ts[..., 2, 2])
    s1 = 2.0 * np.sqrt(np.maximum(1.0 + ts[..., 1, 1] - ts[..., 0, 0] - ts[..., 2, 2], eps))
    qs = np.where(((t <= 0) & c1)[..., np.newaxis].repeat(4, axis=-1), np.concatenate([
        ((ts[..., 0, 2] - ts[..., 2, 0]) / s1)[..., np.newaxis],
        ((ts[..., 0, 1] + ts[..., 1, 0]) / s1)[..., np.newaxis],
        (s1 * 0.25)[..., np.newaxis],
        ((ts[..., 1, 2] + ts[..., 2, 1]) / s1)[..., np.newaxis]
    ], axis=-1), qs)

    c2 = (~c0) & (~c1)
    s2 = 2.0 * np.sqrt(np.maximum(1.0 + ts[..., 2, 2] - ts[..., 0, 0] - ts[..., 1, 1], eps))
    qs = np.where(((t <= 0) & c2)[..., np.newaxis].repeat(4, axis=-1), np.concatenate([
        ((ts[..., 1, 0] - ts[..., 0, 1]) / s2)[..., np.newaxis],
        ((ts[..., 0, 2] + ts[..., 2, 0]) / s2)[..., np.newaxis],
        ((ts[..., 1, 2] + ts[..., 2, 1]) / s2)[..., np.newaxis],
        (s2 * 0.25)[..., np.newaxis]
    ], axis=-1), qs)

    return qs

geno = pm.PyNode('GenoShape')
pm.polyTriangulate(geno)

joints = [
    'Hips',
    'Spine',
    'Spine1',
    'Spine2',
    'Spine3',
    'Neck',
    'Neck1',
    'Head',
    'HeadEnd',
    'RightShoulder',
    'RightArm',
    'RightForeArm',
    'RightHand',
    'RightHandThumb1',
    'RightHandThumb2',
    'RightHandThumb3',
    'RightHandThumb4',
    'RightHandIndex1',
    'RightHandIndex2',
    'RightHandIndex3',
    'RightHandIndex4',
    'RightHandMiddle1',
    'RightHandMiddle2',
    'RightHandMiddle3',
    'RightHandMiddle4',
    'RightHandRing1',
    'RightHandRing2',
    'RightHandRing3',
    'RightHandRing4',
    'RightHandPinky1',
    'RightHandPinky2',
    'RightHandPinky3',
    'RightHandPinky4',
    'RightForeArmEnd',
    'RightArmEnd',
    'LeftShoulder',
    'LeftArm',
    'LeftForeArm',
    'LeftHand',
    'LeftHandThumb1',
    'LeftHandThumb2',
    'LeftHandThumb3',
    'LeftHandThumb4',
    'LeftHandIndex1',
    'LeftHandIndex2',
    'LeftHandIndex3',
    'LeftHandIndex4',
    'LeftHandMiddle1',
    'LeftHandMiddle2',
    'LeftHandMiddle3',
    'LeftHandMiddle4',
    'LeftHandRing1',
    'LeftHandRing2',
    'LeftHandRing3',
    'LeftHandRing4',
    'LeftHandPinky1',
    'LeftHandPinky2',
    'LeftHandPinky3',
    'LeftHandPinky4',
    'LeftForeArmEnd',
    'LeftArmEnd',
    'RightUpLeg',
    'RightLeg',
    'RightFoot',
    'RightToeBase',
    'RightToeBaseEnd',
    'RightLegEnd',
    'RightUpLegEnd',
    'LeftUpLeg',
    'LeftLeg',
    'LeftFoot',
    'LeftToeBase',
    'LeftToeBaseEnd',
    'LeftLegEnd',
    'LeftUpLegEnd',
]

joint_nodes = [pm.PyNode(n) for n in joints]

parents = np.asarray([
    joints.index(str(j.getParent().getName())) if j.getParent() else -1
    for j in joint_nodes
])

print(parents)

skinning = pm.PyNode('skinCluster1')

influences = skinning.influenceObjects()
influence_index = np.asarray([joints.index(node.getName()) for node in influences])
print(influence_index)

weights_all = np.asarray(list(skinning.getWeights('GenoShape')))
weights_order = weights_all.argsort(axis=1)[:,::-1]
vert_bone_weis = np.zeros([len(weights_all), 4], dtype=np.float32)
vert_bone_inds = np.zeros([len(weights_all), 4], dtype=np.uint8)
for i in range(len(weights_all)):
    vert_bone_inds[i] = influence_index[weights_order[i,:4]]
    vert_bone_weis[i] = weights_all[i,weights_order[i,:4]]
    vert_bone_weis[i] = vert_bone_weis[i] / np.sum(vert_bone_weis[i])
    vert_bone_inds[i][vert_bone_weis[i] == 0.0] = 0

print(vert_bone_inds.shape, vert_bone_inds)
print(vert_bone_weis.shape, vert_bone_weis)


vert_posns = np.asarray(geno.getPoints(space='world'))
vert_norms = np.asarray(geno.getNormals(space='world'))
vert_uvs = np.asarray(geno.getUVs()).T

print(vert_norms.shape)
print(vert_posns.shape)
print(vert_uvs.shape)

tris_posns_num, tris_posns = geno.getTriangles()
tris_posns_num, tris_posns = np.asarray(tris_posns_num), np.asarray(tris_posns)
assert np.all(tris_posns_num == 1)
tris_posns = tris_posns.reshape([-1, 3])

tris_uvs_num, tris_uvs = geno.getAssignedUVs()
tris_uvs_num, tris_uvs = np.asarray(tris_uvs_num), np.asarray(tris_uvs)
assert np.all(tris_uvs_num == 3)
tris_uvs = tris_uvs.reshape([-1, 3])

tris_norms_num, tris_norms = geno.getNormalIds()
tris_norms_num, tris_norms = np.asarray(tris_norms_num), np.asarray(tris_norms)
assert np.all(tris_norms_num == 3)
tris_norms = tris_norms.reshape([-1, 3])

print(tris_posns.shape)
print(tris_uvs.shape)
print(tris_norms.shape)

tri_num = len(tris_posns)

assert tri_num == len(tris_posns)
assert tri_num == len(tris_norms)
assert tri_num == len(tris_uvs)

vert_map = {}
final_tris = []
final_posns = []
final_norms = []
final_uvs = []
final_bone_inds = []
final_bone_weis = []

for t in range(tri_num):
    
    pi0, pi1, pi2 = tris_posns[t]
    ni0, ni1, ni2 = tris_norms[t]
    ui0, ui1, ui2 = tris_uvs[t]
    
    p0, p1, p2 = vert_posns[pi0], vert_posns[pi1], vert_posns[pi2]
    n0, n1, n2 = vert_norms[ni0], vert_norms[ni1], vert_norms[ni2]
    u0, u1, u2 = vert_uvs[ui0], vert_uvs[ui1], vert_uvs[ui2]
    bi0, bi1, bi2 = vert_bone_inds[pi0], vert_bone_inds[pi1], vert_bone_inds[pi2]
    bw0, bw1, bw2 = vert_bone_weis[pi0], vert_bone_weis[pi1], vert_bone_weis[pi2]
    
    vert0 = (tuple(p0), tuple(n0), tuple(u0), tuple(bi0), tuple(bw0))
    vert1 = (tuple(p1), tuple(n1), tuple(u1), tuple(bi1), tuple(bw1))
    vert2 = (tuple(p2), tuple(n2), tuple(u2), tuple(bi2), tuple(bw2))
    
    if vert0 in vert_map:
        i0 = vert_map[vert0]
    else:
        i0 = len(final_posns)
        vert_map[vert0] = i0
        final_posns.append(p0)
        final_norms.append(n0)
        final_uvs.append(u0)
        final_bone_inds.append(bi0)
        final_bone_weis.append(bw0)
    
    if vert1 in vert_map:
        i1 = vert_map[vert1]
    else:
        i1 = len(final_posns)
        vert_map[vert1] = i1
        final_posns.append(p1)
        final_norms.append(n1)
        final_uvs.append(u1)
        final_bone_inds.append(bi1)
        final_bone_weis.append(bw1)
    
    if vert2 in vert_map:
        i2 = vert_map[vert2]
    else:
        i2 = len(final_posns)
        vert_map[vert2] = i2
        final_posns.append(p2)
        final_norms.append(n2)
        final_uvs.append(u2)
        final_bone_inds.append(bi2)
        final_bone_weis.append(bw2)
    
    final_tris.append((i0, i1, i2))

final_tris = np.asarray(final_tris).astype(np.uint16)
final_posns = np.asarray(final_posns).astype(np.float32)
final_norms = np.asarray(final_norms).astype(np.float32)
final_uvs = np.asarray(final_uvs).astype(np.float32)
final_bone_inds = np.asarray(final_bone_inds).astype(np.uint8)
final_bone_weis = np.asarray(final_bone_weis).astype(np.float32)

print(final_tris.shape, final_tris[:10])
print(final_posns.shape, final_posns[:10])
print(final_norms.shape, final_norms[:10])
print(final_uvs.shape, final_uvs[:10])
print(final_bone_inds.shape, final_bone_inds[:10])
print(final_bone_weis.shape, final_bone_weis[:10])

bone_xforms = np.asarray([pm.xform(j, q=True, ws=True, m=True) for j in joints]).reshape([len(joints), 4, 4]).transpose([0, 2, 1])
#print(bone_xforms)

bone_positions = bone_xforms[:,:3,3].copy().astype(np.float32)
bone_rotations = quat_from_xform(bone_xforms).astype(np.float32)
bone_parents = parents.astype(np.int32)

bone_rotations = np.concatenate([
    bone_rotations[:,1:2],
    bone_rotations[:,2:3],
    bone_rotations[:,3:4],
    bone_rotations[:,0:1],
], axis=-1)

#print(bone_positions)
#print(bone_rotations)

final_posns = 0.01 * final_posns
bone_positions = 0.01 * bone_positions

with open('C:/Projects/GenoView/resources/Geno.bin', 'wb') as f:

    f.write(struct.pack('I', len(final_posns)))
    f.write(struct.pack('I', len(final_tris)))
    f.write(struct.pack('I', len(joints)))
    f.write(final_posns.tobytes())
    f.write(final_uvs.tobytes())
    f.write(final_norms.tobytes())
    f.write(final_bone_inds.tobytes())
    f.write(final_bone_weis.tobytes())
    f.write(final_tris.tobytes())
    
    
    for i in range(len(bone_parents)):
        f.write(struct.pack('32si', bytes(joints[i], encoding='ascii'), bone_parents[i]))
    
    for i in range(len(bone_parents)):
        f.write(struct.pack('ffffffffff',
            bone_positions[i,0], bone_positions[i,1], bone_positions[i,2],
            bone_rotations[i,0], bone_rotations[i,1], bone_rotations[i,2], bone_rotations[i,3],
            1.0, 1.0, 1.0))
        