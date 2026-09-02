// -*- ispc -*-
// @file skinning.ispc
// @brief Batch matrix palette multiplication and vertex deformation kernels.
//
// Compiled to SSE2/SSE4/AVX/AVX2 object files with automatic runtime ISA dispatch
// via the ISPC multi-target compilation path. Included in C++ via the auto-generated
// skinning_ispc.h header.

// ---------------------------------------------------------------------------
// Matrix palette batch multiply: C[i] = A[i] * B[i]
// ---------------------------------------------------------------------------
export void ispc_matMul_aos(
    uniform const float A[],
    uniform const float B[],
    uniform float Res[],
    uniform int count)
{
    foreach (i = 0 ... count)
    {
        float a[4][4];
        float b[4][4];
        
        // Load the 4x4 matrices from the AOS arrays
        for (uniform int r = 0; r < 4; ++r)
        {
            for (uniform int c = 0; c < 4; ++c)
            {
                a[r][c] = A[i*16 + r*4 + c];
                b[r][c] = B[i*16 + r*4 + c];
            }
        }
        
        float res[4][4];
        // Row-major matrix product:
        // res.row[r] = a.row[r][0]*b.row[0] + a.row[r][1]*b.row[1] + a.row[r][2]*b.row[2] + a.row[r][3]*b.row[3]
        for (uniform int r = 0; r < 4; ++r)
        {
            for (uniform int c = 0; c < 4; ++c)
            {
                res[r][c] = a[r][0]*b[0][c] + a[r][1]*b[1][c] + a[r][2]*b[2][c] + a[r][3]*b[3][c];
            }
        }
        
        // Store back to AOS
        for (uniform int r = 0; r < 4; ++r)
        {
            for (uniform int c = 0; c < 4; ++c)
            {
                Res[i*16 + r*4 + c] = res[r][c];
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Batch vertex deformation: dst = (sum_k w_k * Palette[idx_k]) * (BindShape * v)
// ---------------------------------------------------------------------------
export void ispc_skin_vertices(
    uniform const float mat_palette[], 
    uniform const float bind_shape[], 
    uniform const float v_in[],
    uniform const float weights[],
    uniform const unsigned int8 joint_indices[],
    uniform float v_out[],
    uniform int num_vertices)
{
    float b_shape[4][4];
    for (uniform int r = 0; r < 4; ++r)
    {
        for (uniform int c = 0; c < 4; ++c)
        {
            b_shape[r][c] = bind_shape[r*4 + c];
        }
    }

    foreach (i = 0 ... num_vertices)
    {
        // Load input vertex (x, y, z, 1.0)
        float vx = v_in[i*4 + 0];
        float vy = v_in[i*4 + 1];
        float vz = v_in[i*4 + 2];
        float vw = 1.0f;

        // t = bind_shape * v
        float tx = vx * b_shape[0][0] + vy * b_shape[1][0] + vz * b_shape[2][0] + vw * b_shape[3][0];
        float ty = vx * b_shape[0][1] + vy * b_shape[1][1] + vz * b_shape[2][1] + vw * b_shape[3][1];
        float tz = vx * b_shape[0][2] + vy * b_shape[1][2] + vz * b_shape[2][2] + vw * b_shape[3][2];
        float tw = 1.0f;

        // Load 4 weights and bone indices
        float w0 = weights[i*4 + 0];
        float w1 = weights[i*4 + 1];
        float w2 = weights[i*4 + 2];
        float w3 = weights[i*4 + 3];

        int i0 = joint_indices[i*4 + 0];
        int i1 = joint_indices[i*4 + 1];
        int i2 = joint_indices[i*4 + 2];
        int i3 = joint_indices[i*4 + 3];

        // Compute blended final_mat
        float final_mat[4][4];
        for (uniform int r = 0; r < 4; ++r)
        {
            for (uniform int c = 0; c < 4; ++c)
            {
                final_mat[r][c] = 
                    w0 * mat_palette[i0*16 + r*4 + c] +
                    w1 * mat_palette[i1*16 + r*4 + c] +
                    w2 * mat_palette[i2*16 + r*4 + c] +
                    w3 * mat_palette[i3*16 + r*4 + c];
            }
        }

        // dst = final_mat * t
        float dx = tx * final_mat[0][0] + ty * final_mat[1][0] + tz * final_mat[2][0] + tw * final_mat[3][0];
        float dy = tx * final_mat[0][1] + ty * final_mat[1][1] + tz * final_mat[2][1] + tw * final_mat[3][1];
        float dz = tx * final_mat[0][2] + ty * final_mat[1][2] + tz * final_mat[2][2] + tw * final_mat[3][2];

        v_out[i*4 + 0] = dx;
        v_out[i*4 + 1] = dy;
        v_out[i*4 + 2] = dz;
        v_out[i*4 + 3] = 1.0f;
    }
}
