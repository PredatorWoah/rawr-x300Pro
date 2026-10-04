from pathlib import Path
s=(Path(__file__).resolve().parents[1]/'tools/vng4_reference.py').read_text()
required=[
 'wt=np.float32(1<<shift)',
 'np.float32(mx * np.float32(0.5))',
 's0=np.float32(0.0);s1=np.float32(0.0)',
 'np.float32(2.0)*np.float32(num)',
 'np.float32(0.25)*rb',
]
for token in required:
    assert token in s, token
print('VNG4_FLOAT32_ORACLE_AUDIT_PASS canonical_scalar_semantics=true')
