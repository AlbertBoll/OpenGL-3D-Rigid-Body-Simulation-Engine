#pragma once

#include<glm/glm.hpp>
#include <Math/Math.h>

namespace GEngine
{
	struct BoneInfo
	{
		/*id is index in finalBoneMatrices*/
		int id;

		/*offset matrix transforms vertex from model space to bone space*/
		Math::Mat4 offset;

	};

}
#pragma once
