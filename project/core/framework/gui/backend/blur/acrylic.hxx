#pragma once

#include <memory>
#include <windows.h>
#include <core/framework/gui/backend/math/math.hxx>

namespace core::gui
{
	class c_acrylic
	{
	public:
		bool create( HWND )
		{
			return true;
		}

		void sync( const c_vector_2d&, const c_vector_2d&, bool )
		{
		}

		void destroy( )
		{
		}
	};

	inline std::shared_ptr<c_acrylic> g_acrylic = std::make_shared<c_acrylic>( );
}
