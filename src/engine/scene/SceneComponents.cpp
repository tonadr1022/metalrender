#include "engine/scene/SceneComponents.hpp"

#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace teng::engine {

glm::mat4 transform_to_matrix(const Transform& transform) {
  const glm::mat4 translation = glm::translate(glm::mat4{1.f}, transform.translation);
  const glm::mat4 rotation = glm::mat4_cast(transform.rotation);
  const glm::mat4 scale = glm::scale(glm::mat4{1.f}, transform.scale);
  return translation * rotation * scale;
}

}  // namespace teng::engine
