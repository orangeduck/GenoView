import struct
import numpy as np
import bvh
import quat

bvh_files = [
    'ground1_subject1.bvh',
    'ground2_subject2.bvh',
    'kthstreet_gPO_sFM_cAll_d02_mPO_ch01_atombounce_001.bvh',
]

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

for bvh_file in bvh_files:
    
    bvh_data = bvh.load(bvh_file)
    
    positions = bvh_data['positions'].copy()
    parents = bvh_data['parents'].copy()
    names = bvh_data['names'].copy()
    rotations = quat.unroll(quat.from_euler(np.radians(bvh_data['rotations']), order=bvh_data['order']))
    rotations, positions = quat.fk(rotations, positions, parents)
    
    assert names == joints
    
    # Swap order
    rotations = np.concatenate([
        rotations[...,1:2],
        rotations[...,2:3],
        rotations[...,3:4],
        rotations[...,0:1],
    ], axis=-1).astype(np.float32)

    # Convert from cm to m
    positions = (0.01 * positions).astype(np.float32)
    
    with open(bvh_file.replace('.bvh', '.bin'), 'wb') as f:
        
        nframes = positions.shape[0]
        nbones = positions.shape[1]

        f.write(struct.pack('ii', nframes, nbones))
        
        for i in range(nbones):
            f.write(struct.pack('32si', bytes(names[i], encoding='ascii'), parents[i]))
        
        for i in range(nframes):
            for j in range(nbones):
                f.write(struct.pack('ffffffffff',
                    positions[i,j,0], positions[i,j,1], positions[i,j,2],
                    rotations[i,j,0], rotations[i,j,1], rotations[i,j,2], rotations[i,j,3],
                    1.0, 1.0, 1.0
                ))
            
        f.write(struct.pack('32s', bytes(bvh_file.replace('.bvh',''), encoding='ascii')[:31]))
        