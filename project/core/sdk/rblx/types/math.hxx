#pragma once

namespace sdk::math
{
	struct vector2_t
	{
		float x, y;

		vector2_t( float x = 0, float y = 0 ) : x( x ), y( y ) { }

		inline vector2_t operator+( const vector2_t& vector ) const
		{
			return
			{
				x + vector.x,
				y + vector.y
			};
		}

		inline vector2_t operator-( const vector2_t& vector ) const
		{
			return
			{
				x - vector.x,
				y - vector.y
			};
		}

		inline vector2_t operator*( const vector2_t& vector ) const
		{
			return
			{
			   x * vector.x,
			   y * vector.y
			};
		}

		inline vector2_t operator/( const vector2_t& vector ) const
		{
			return
			{
			   this->x / vector.x,
			   this->y / vector.y
			};
		}

		inline const bool empty( ) const
		{
			return x == 0.f && y == 0.f;
		}

		inline const float distance( const vector2_t& vector ) const
		{
			return sqrtf(
				powf( x - vector.x, 2.0 ) + powf( y - vector.y, 2.0 )
			);
		}

		inline const float magnitude( ) const
		{
			return sqrtf(
				( x * x ) + ( y * y )
			);
		}
	};

	struct vector3_t
	{
		float x, y, z;

		vector3_t( float x = 0, float y = 0, float z = 0 ) : x( x ), y( y ), z( z ) { }

		inline const float& operator[]( int i ) const
		{
			return ( ( float* ) this )[i];
		}

		inline float& operator[]( int i )
		{
			return ( ( float* ) this )[i];
		}

		inline vector3_t __fastcall operator/( float s ) const
		{
			return *this * ( 1.0f / s );
		}

		inline vector3_t& operator+=( const vector3_t& other )
		{
			x += other.x;
			y += other.y;
			z += other.z;
			return *this;
		}

		inline vector3_t& operator-=( const vector3_t& other )
		{
			x -= other.x;
			y -= other.y;
			z -= other.z;
			return *this;
		}

		inline bool operator==( const vector3_t& other ) const
		{
			return x == other.x && y == other.y && z == other.z;
		}
		inline vector3_t operator-( ) const
		{ // returns all negatives
			return { -x, -y, -z };
		}

		inline vector3_t operator+( const vector3_t& vector ) const
		{
			return
			{
				x + vector.x,
				y + vector.y,
				z + vector.z
			};
		}

		inline vector3_t operator-( const vector3_t& vector ) const
		{
			return
			{
				x - vector.x,
				y - vector.y,
				z - vector.z
			};
		}

		inline vector3_t operator*( const vector3_t& vector ) const
		{
			return
			{
				x * vector.x,
				y * vector.y,
				z * vector.z
			};
		}

		inline vector3_t operator*( float s ) const
		{
			return { x * s, y * s, z * s };
		}

		inline vector3_t operator/( const vector3_t& vector ) const
		{
			return
			{
				x / vector.x,
				y / vector.y,
				z / vector.z
			};
		}

		inline bool empty( ) const
		{
			return x == 0.f && y == 0.f && z == 0.f;
		}

		inline const float dot( const vector3_t& vector ) const
		{
			return x * vector.x + y * vector.y + z * vector.z;
		}

		inline const float distance( const vector3_t& vector ) const
		{
			return sqrtf(
				powf( x - vector.x, 2.0 ) + powf( y - vector.y, 2.0 ) + powf( vector.z - z, 2.0 )
			);
		}

		inline const float magnitude( ) const
		{
			return sqrtf(
				( x * x ) + ( y * y ) + ( z * z )
			);
		}

		inline vector3_t normalized( ) const
		{
			float mag = magnitude( );

			if ( mag == 0 ) return *this;

			return vector3_t(
				x / mag,
				y / mag,
				z / mag
			);
		}

		inline vector3_t cross( const vector3_t& other ) const
		{
			return
			{
				y * other.z - z * other.y,
				z * other.x - x * other.z,
				x * other.y - y * other.x
			};
		}

		inline const float squared( ) const
		{
			return x * x + y * y + z * z;
		}

		inline vector3_t direction( ) const
		{
			const float len_squared = squared( );
			const float inv_sqrt = 1.0f / sqrtf( len_squared );

			return vector3_t( x * inv_sqrt, y * inv_sqrt, z * inv_sqrt );
		}

		inline static vector3_t lerp( const vector3_t& from, const vector3_t& to, float f )
		{
			vector3_t result;

			result.x = from.x + ( to.x - from.x ) * f;
			result.y = from.y + ( to.y - from.y ) * f;
			result.z = from.z + ( to.z - from.z ) * f;

			return result;
		}
	};

	struct vector4_t
	{
		float x, y, z, w;

		vector4_t( float x = 0, float y = 0, float z = 0, float w = 0 ) : x( x ), y( y ), z( z ), w( w ) { }

		inline const float& operator[]( int i ) const
		{
			return ( ( float* ) this )[i];
		}

		inline float& operator[]( int i )
		{
			return ( ( float* ) this )[i];
		}

		inline vector4_t operator+( const vector4_t& vector ) const
		{
			return
			{
			   x + vector.x,
			   y + vector.y,
			   z + vector.z,
			   w + vector.w
			};
		}

		inline vector4_t operator-( const vector4_t& vector ) const
		{
			return
			{
				x - vector.x,
				y - vector.y,
				z - vector.z,
				w - vector.w
			};
		}

		inline vector4_t operator*( const vector4_t& vector ) const
		{
			return
			{
				x * vector.x,
				y * vector.y,
				z * vector.z,
				w * vector.w
			};
		}

		inline vector4_t operator/( const vector4_t& vector ) const
		{
			return
			{
			   x / vector.x,
			   y / vector.y,
			   z / vector.z,
			   w / vector.w
			};
		}

		inline vector3_t xyz( ) const
		{
			return vector3_t( x, y, z );
		}

		inline const float dot( const vector4_t& vector ) const
		{
			return x * vector.x + y * vector.y + z * vector.z + w * vector.w;
		}
	};

	struct matrix_t
	{
		float data[16]; // lol...
	};

	struct matrix3_t
	{
		float data[3][3];

		inline float* operator[]( int i )
		{
			return ( float* ) &data[i][0];
		}

		inline const float* operator[]( int i ) const
		{
			return ( const float* ) &data[i][0];
		}

		inline matrix3_t operator+( const matrix3_t& other ) const
		{
			matrix3_t result;

			for ( int i = 0; i < 3; ++i )
				for ( int j = 0; j < 3; ++j )
					result.data[i][j] = data[i][j] + other.data[i][j];

			return result;
		}

		inline matrix3_t operator-( const matrix3_t& other ) const
		{
			matrix3_t result;

			for ( int i = 0; i < 3; ++i )
				for ( int j = 0; j < 3; ++j )
					result.data[i][j] = data[i][j] - other.data[i][j];

			return result;
		}

		inline matrix3_t operator*( const matrix3_t& other ) const
		{
			matrix3_t result;

			for ( int i = 0; i < 3; ++i )
				for ( int j = 0; j < 3; ++j )
					result.data[i][j] = data[i][j] * other.data[i][j];

			return result;
		}

		inline matrix3_t operator/( const matrix3_t& other ) const
		{
			matrix3_t result;

			for ( int i = 0; i < 3; ++i )
				for ( int j = 0; j < 3; ++j )
					result.data[i][j] = data[i][j] / other.data[i][j];

			return result;
		}

		inline static matrix3_t identity( )
		{
			matrix3_t result {};
			result.data[0][0] = 1.0f;
			result.data[1][1] = 1.0f;
			result.data[2][2] = 1.0f;
			return result;
		}

		inline void set_column( int index, const vector3_t& vec )
		{
			data[0][index] = vec.x;
			data[1][index] = vec.y;
			data[2][index] = vec.z;
		}

		inline matrix3_t normalize( ) const
		{
			matrix3_t result = *this;

			for ( int col = 0; col < 3; ++col )
			{
				float len = std::sqrt(
					result.data[0][col] * result.data[0][col] +
					result.data[1][col] * result.data[1][col] +
					result.data[2][col] * result.data[2][col] );

				if ( len > 0.0f )
				{
					result.data[0][col] /= len;
					result.data[1][col] /= len;
					result.data[2][col] /= len;
				}
			}

			return result;
		}

		inline static matrix3_t from_euler_angles_xyz( const float& xangle, const float& yangle, const float& zangle )
		{
			float cx = cos( xangle );		float sx = sin( xangle );
			float cy = cos( yangle );		float sy = sin( yangle );
			float cz = cos( zangle );		float sz = sin( zangle );

			matrix3_t result;

			// right vector
			result.data[0][0] = cy * cz;					// r00
			result.data[1][0] = sx * sy * cz + cx * sz;		// r10
			result.data[2][0] = -cx * sy * cz + sx * sz;	// r20

			// up vector
			result.data[0][1] = -cy * sz;					// r01
			result.data[1][1] = -sx * sy * sz + cx * cz;	// r11
			result.data[2][1] = cx * sy * sz + sx * cz;		// r21

			// look vector
			result.data[0][2] = sy;							// r02
			result.data[1][2] = -sx * cy;					// r12
			result.data[2][2] = cx * cy;					// r22

			return result;
		}

		inline static matrix3_t from_euler_angles_yxz( const float& xangle, const float& yangle, const float& zangle )
		{
			float cx = cos( xangle );		float sx = sin( xangle );
			float cy = cos( yangle );		float sy = sin( yangle );
			float cz = cos( zangle );		float sz = sin( zangle );

			matrix3_t result;

			// right vector
			result.data[0][0] = cy * cz - sy * sx * sz;		// r00
			result.data[1][0] = -cy * sz - sy * sx * cz;	// r10
			result.data[2][0] = -sy * cx;					// r20

			// up vector
			result.data[0][1] = cx * sz;					// r01
			result.data[1][1] = cx * cz;					// r11
			result.data[2][1] = -sx;						// r21

			// look vector
			result.data[0][2] = sy * cz + cy * sx * sz;		// r02
			result.data[1][2] = sy * sz - cy * sx * cz;		// r12
			result.data[2][2] = cy * cx;					// r22

			return result;
		}

		inline static matrix3_t from_axis_angle( const vector3_t& axis, float angle )
		{
			vector3_t u = axis.normalized( );

			float cos_a = cos( angle );
			float sin_a = sin( angle );
			float one_minus_cos_a = 1.0f - cos_a;

			matrix3_t rotation_matrix;

			rotation_matrix.data[0][0] = cos_a + u.x * u.x * one_minus_cos_a;
			rotation_matrix.data[0][1] = u.x * u.y * one_minus_cos_a - u.z * sin_a;
			rotation_matrix.data[0][2] = u.x * u.z * one_minus_cos_a + u.y * sin_a;

			rotation_matrix.data[1][0] = u.y * u.x * one_minus_cos_a + u.z * sin_a;
			rotation_matrix.data[1][1] = cos_a + u.y * u.y * one_minus_cos_a;
			rotation_matrix.data[1][2] = u.y * u.z * one_minus_cos_a - u.x * sin_a;

			rotation_matrix.data[2][0] = u.z * u.x * one_minus_cos_a - u.y * sin_a;
			rotation_matrix.data[2][1] = u.z * u.y * one_minus_cos_a + u.x * sin_a;
			rotation_matrix.data[2][2] = cos_a + u.z * u.z * one_minus_cos_a;

			return rotation_matrix;
		}

		inline static matrix3_t lerp( const matrix3_t& from, const matrix3_t& to, float f )
		{
			matrix3_t result;
			for ( int i = 0; i < 3; ++i )
			{
				for ( int j = 0; j < 3; ++j )
				{
					result.data[i][j] = from.data[i][j] + ( to.data[i][j] - from.data[i][j] ) * f;
				}
			}
			return result;
		}

		inline static matrix3_t look_at( const vector3_t& look, const vector3_t& at, const vector3_t& up_vector = { 0, 1, 0 } )
		{
			vector3_t forward = ( at - look ).normalized( );
			vector3_t world_up = up_vector;
			vector3_t right = world_up.cross( forward ).normalized( );
			vector3_t up = forward.cross( right );

			matrix3_t result;

			result.data[0][0] = -right.x;
			result.data[1][0] = -right.y;
			result.data[2][0] = -right.z;

			result.data[0][1] = up.x;
			result.data[1][1] = up.y;
			result.data[2][1] = up.z;

			result.data[0][2] = -forward.x;
			result.data[1][2] = -forward.y;
			result.data[2][2] = -forward.z;

			return result;
		}

		inline vector3_t column( int column ) const
		{
			return
			{
				data[0][column],
				data[1][column],
				data[2][column]
			};
		}

		inline vector3_t row( int row ) const
		{
			return
			{
				data[row][0],
				data[row][1],
				data[row][2]
			};
		}
	};

	struct matrix4_t
	{
		float data[4][4];

		inline const auto operator[]( int i ) const noexcept
		{
			return data[i];
		}

		inline matrix4_t operator+( const matrix4_t& other ) const
		{
			matrix4_t result;

			for ( int i = 0; i < 4; ++i )
				for ( int j = 0; j < 4; ++j )
					result.data[i][j] = data[i][j] + other.data[i][j];

			return result;
		}

		inline matrix4_t operator-( const matrix4_t& other ) const
		{
			matrix4_t result;

			for ( int i = 0; i < 4; ++i )
				for ( int j = 0; j < 4; ++j )
					result.data[i][j] = data[i][j] - other.data[i][j];

			return result;
		}

		inline matrix4_t operator*( const matrix4_t& other ) const
		{
			matrix4_t result;

			for ( int i = 0; i < 4; ++i )
				for ( int j = 0; j < 4; ++j )
					result.data[i][j] = data[i][j] * other.data[i][j];

			return result;
		}

		inline matrix4_t operator/( const matrix4_t& other ) const
		{
			matrix4_t result;

			for ( int i = 0; i < 4; ++i )
				for ( int j = 0; j < 4; ++j )
					result.data[i][j] = data[i][j] / other.data[i][j];

			return result;
		}

		inline static matrix4_t identity( )
		{
			matrix4_t result {};
			for ( int i = 0; i < 4; ++i )
				result.data[i][i] = 1.0f;
			return result;
		}


		inline vector4_t operator*( vector3_t mul ) const
		{
			vector4_t result;

			result.x = data[0][0] * mul.x + data[0][1] * mul.y + data[0][2] * mul.z + data[0][3];
			result.y = data[1][0] * mul.x + data[1][1] * mul.y + data[1][2] * mul.z + data[1][3];
			result.z = data[2][0] * mul.x + data[2][1] * mul.y + data[2][2] * mul.z + data[2][3];
			result.w = data[3][0] * mul.x + data[3][1] * mul.y + data[3][2] * mul.z + data[3][3];

			return result;
		}

		inline float sub_determinant( int exclude_row, int exclude_colomn ) const
		{
			int row[3];
			int col[3];

			for ( int i = 0; i < 3; ++i )
			{
				row[i] = i;
				col[i] = i;

				if ( i >= exclude_row )
				{
					++row[i];
				}

				if ( i >= exclude_colomn )
				{
					++col[i];
				}
			}

			float cofactor00 = data[row[1]][col[1]] * data[row[2]][col[2]] - data[row[1]][col[2]] * data[row[2]][col[1]];
			float cofactor10 = data[row[1]][col[2]] * data[row[2]][col[0]] - data[row[1]][col[0]] * data[row[2]][col[2]];
			float cofactor20 = data[row[1]][col[0]] * data[row[2]][col[1]] - data[row[1]][col[1]] * data[row[2]][col[0]];

			return data[row[0]][col[0]] * cofactor00 + data[row[0]][col[1]] * cofactor10 + data[row[0]][col[2]] * cofactor20;
		}

		inline matrix4_t cofactor( ) const
		{
			matrix4_t result;

			int i = 1;

			for ( int r = 0; r < 4; ++r )
			{
				for ( int c = 0; c < 4; ++c )
				{
					float det = sub_determinant( r, c );
					result.data[r][c] = i * det;
					i = -i;
				}
				i = -i;
			}

			return result;
		}

		inline const vector4_t& row( int row ) const
		{
			return reinterpret_cast< const vector4_t* >( data[row] )[0];
		}

		inline vector4_t column( int column ) const
		{
			vector4_t result;

			for ( int r = 0; r < 4; ++r )
			{
				result[r] = data[r][column];
			}
			return result;
		}

		inline float determinant( ) const
		{
			return cofactor( ).row( 0 ).dot( row( 0 ) );
		}

		inline matrix4_t transpose( ) const
		{
			matrix4_t result;

			for ( int r = 0; r < 4; ++r )
			{
				for ( int c = 0; c < 4; ++c )
				{
					result.data[c][r] = data[r][c];
				}
			}

			return result;
		}

		inline matrix4_t adjoint( ) const
		{
			return cofactor( ).transpose( );
		}

		inline matrix4_t operator*( const float s ) const
		{
			matrix4_t result;

			for ( int r = 0; r < 4; ++r )
			{
				for ( int c = 0; c < 4; ++c )
				{
					result.data[r][c] = data[r][c] * s;
				}
			}

			return result;
		}

		inline matrix4_t inverse( ) const
		{
			matrix4_t ad = adjoint( );

			float det = ad.column( 0 ).dot( row( 0 ) );

			return ad * ( 1.0f / det );
		}

		vector4_t operator*( const vector4_t& vector ) const
		{
			vector4_t result( 0, 0, 0, 0 );

			for ( int r = 0; r < 4; ++r )
			{
				for ( int c = 0; c < 4; ++c )
				{
					result[r] += data[r][c] * vector[c];
				}
			}

			return result;
		}
	};

	struct cframe_t
	{
		/*						 Rotation Table (Matrix3)						/*
			xVector/RightVector		yVector/UpVector		zVector/-LookVector
			-------------------		----------------		-------------------
					R00					  R01						R02
					R10					  R11						R12
					R20					  R21						R22
		*/

		matrix3_t rotation;
		vector3_t position;

		inline vector3_t right_vector( ) const
		{
			return rotation.column( 0 );
		}

		inline vector3_t up_vector( ) const
		{
			return rotation.column( 1 );
		}

		inline vector3_t look_vector( ) const
		{
			return -rotation.column( 2 );
		}

		static inline cframe_t identity( )
		{
			cframe_t result;

			result.rotation = matrix3_t::identity( );
			result.position = vector3_t( 0, 0, 0 );

			return result;
		}

		inline static cframe_t _new( const vector3_t& right, const vector3_t& up, const vector3_t& look, const vector3_t& position )
		{
			cframe_t result;

			result.rotation.set_column( 0, right );
			result.rotation.set_column( 1, up );
			result.rotation.set_column( 2, look );
			result.position = position;

			return result;
		}

		inline static cframe_t _new( float x, float y, float z,
			float R00, float R01, float R02,
			float R10, float R11, float R12,
			float R20, float R21, float R22 )
		{
			cframe_t cframe;

			cframe.position = vector3_t( x, y, z );

			cframe.rotation[0][0] = R00; cframe.rotation[0][1] = R01; cframe.rotation[0][2] = R02;
			cframe.rotation[1][0] = R10; cframe.rotation[1][1] = R11; cframe.rotation[1][2] = R12;
			cframe.rotation[2][0] = R20; cframe.rotation[2][1] = R21; cframe.rotation[2][2] = R22;

			return cframe;
		}

		inline cframe_t operator+( const cframe_t& other ) const
		{
			return from_matrix( rotation + other.rotation, position + other.position );
		}

		inline cframe_t operator-( const cframe_t& other ) const
		{
			return from_matrix( rotation - other.rotation, position - other.position );
		}

		inline cframe_t operator*( const cframe_t& other ) const
		{
			return from_matrix( rotation * other.rotation, position * other.position );
		}

		inline cframe_t operator/( const cframe_t& other ) const
		{
			return from_matrix( rotation / other.rotation, position / other.position );
		}

		inline static cframe_t from_euler_angles_xyz( const float& rx, const float& ry, const float& rz )
		{
			float cx = cos( rx );		float sx = sin( rx );
			float cy = cos( ry );		float sy = sin( ry );
			float cz = cos( rz );		float sz = sin( rz );

			cframe_t result;

			// right vector
			result.rotation.data[0][0] = cy * cz;					// r00
			result.rotation.data[1][0] = sx * sy * cz + cx * sz;	// r10
			result.rotation.data[2][0] = -cx * sy * cz + sx * sz;	// r20

			// up vector
			result.rotation.data[0][1] = -cy * sz;					// r01
			result.rotation.data[1][1] = -sx * sy * sz + cx * cz;	// r11
			result.rotation.data[2][1] = cx * sy * sz + sx * cz;	// r21

			// look vector
			result.rotation.data[0][2] = sy;						// r02
			result.rotation.data[1][2] = -sx * cy;					// r12
			result.rotation.data[2][2] = cx * cy;					// r22

			return result;
		}

		inline static cframe_t from_euler_angles_yxz( const float& rx, const float& ry, const float& rz )
		{
			float cx = cos( rx );		float sx = sin( rx );
			float cy = cos( ry );		float sy = sin( ry );
			float cz = cos( rz );		float sz = sin( rz );

			cframe_t result;

			// right vector
			result.rotation.data[0][0] = cy * cz - sy * sx * sz;	// r00
			result.rotation.data[1][0] = -cy * sz - sy * sx * cz;	// r10
			result.rotation.data[2][0] = -sy * cx;					// r20

			// up vector
			result.rotation.data[0][1] = cx * sz;					// r01
			result.rotation.data[1][1] = cx * cz;					// r11
			result.rotation.data[2][1] = -sx;						// r21

			// look vector
			result.rotation.data[0][2] = sy * cz + cy * sx * sz;	// r02
			result.rotation.data[1][2] = sy * sz - cy * sx * cz;	// r12
			result.rotation.data[2][2] = cy * cx;					// r22

			return result;
		}

		inline static cframe_t from_euler_angles( const float& rx, const float& ry, const float& rz )
		{
			float cx = cos( rx );		float sx = sin( rx );
			float cy = cos( ry );		float sy = sin( ry );
			float cz = cos( rz );		float sz = sin( rz );

			cframe_t result;

			// right vector
			result.rotation.data[0][0] = cy * cz;					// r00
			result.rotation.data[1][0] = sx * sy * cz + cx * sz;	// r10
			result.rotation.data[2][0] = -cx * sy * cz + sx * sz;	// r20

			// up vector
			result.rotation.data[0][1] = -cy * sz;					// r01
			result.rotation.data[1][1] = -sx * sy * sz + cx * cz;	// r11
			result.rotation.data[2][1] = cx * sy * sz + sx * cz;	// r21

			// look vector
			result.rotation.data[0][2] = sy;						// r02
			result.rotation.data[1][2] = -sx * cy;					// r12
			result.rotation.data[2][2] = cx * cy;					// r22

			return result;
		}

		inline static cframe_t from_orientation( const float& rx, const float& ry, const float& rz )
		{
			float cx = cos( rx );		float sx = sin( rx );
			float cy = cos( ry );		float sy = sin( ry );
			float cz = cos( rz );		float sz = sin( rz );

			cframe_t result;
			// right vector
			result.rotation.data[0][0] = cy * cz - sy * sx * sz;	// r00
			result.rotation.data[1][0] = -cy * sz - sy * sx * cz;	// r10
			result.rotation.data[2][0] = -sy * cx;					// r20
			// up vector
			result.rotation.data[0][1] = cx * sz;					// r01
			result.rotation.data[1][1] = cx * cz;					// r11
			result.rotation.data[2][1] = -sx;						// r21
			// look vector
			result.rotation.data[0][2] = sy * cz + cy * sx * sz;	// r02
			result.rotation.data[1][2] = sy * sz - cy * sx * cz;	// r12
			result.rotation.data[2][2] = cy * cx;					// r22

			return result;
		}
		//
		inline static cframe_t look_at( const vector3_t& look, const vector3_t& at, const vector3_t& up_vector = { 0, 1, 0 } )
		{
			vector3_t forward = ( at - look ).normalized( );
			vector3_t world_up = up_vector;
			vector3_t right = world_up.cross( forward ).normalized( );
			vector3_t up = forward.cross( right );

			cframe_t result;

			result.rotation.data[0][0] = -right.x;
			result.rotation.data[1][0] = -right.y;
			result.rotation.data[2][0] = -right.z;

			result.rotation.data[0][1] = up.x;
			result.rotation.data[1][1] = up.y;
			result.rotation.data[2][1] = up.z;

			result.rotation.data[0][2] = -forward.x;
			result.rotation.data[1][2] = -forward.y;
			result.rotation.data[2][2] = -forward.z;

			return result;
		}

		inline static cframe_t from_matrix( const matrix3_t& rotation, const vector3_t& position )
		{
			cframe_t cframe;

			cframe.rotation = rotation;
			cframe.position = position;

			return cframe;
		}
	};

	struct color3_t
	{
		float r, g, b;

		color3_t( float r = 0, float g = 0, float b = 0 ) : r( r ), g( g ), b( b ) { };
	};
}