
#include "Constants.h"

#include "engine/engine.h"
#include "engine/mesh.h"
#include "engine/model.h"
#include "game.h"

using namespace std;

ModelNT::ModelNT(Engine *engine, const char *meshFilepath, uint32_t modelID)
    : m_Engine(engine),
      m_Mesh(nullptr),
      m_MeshFilepath(meshFilepath),
      m_InitialTransform(),
      m_CurrentTransform(),
      m_RotationAnimationVector(vec3(0.0f, 0.0f, 0.0f)) {
    m_Mesh = std::make_unique<MeshNT>(engine, meshFilepath);
    m_ModelID = modelID;
    validate();
}

ModelNT::ModelNT(Engine *engine, const char *meshFilepath, uint32_t modelID, Transform initialTransform)
    : m_Engine(engine),
      m_Mesh(nullptr),
      m_MeshFilepath(meshFilepath),
      m_InitialTransform(initialTransform),
      m_CurrentTransform(initialTransform) {
    m_Mesh = std::make_unique<MeshNT>(engine, meshFilepath);
    m_ModelID = modelID;

    validate();
}

void ModelNT::validate() {
    cout << "Validating mesh.\n";
    if (!m_Mesh) throw runtime_error("Mesh of Model not set!");
    m_Mesh->validate();
    cout << "Mesh is valid.\n";
}

DEF ModelNT::translate(const vec3 &deltaPosition) -> void { m_CurrentTransform.translate(deltaPosition); }
DEF ModelNT::rotate(const vec3 &deltaRotation) -> void { m_CurrentTransform.rotateEuler(deltaRotation); }
DEF ModelNT::scaleBy(const vec3 &scaleFactor) -> void { m_CurrentTransform.scaleBy(scaleFactor); }
DEF ModelNT::resetTransform() -> void { m_CurrentTransform = m_InitialTransform; }

DEF ModelNT::getMesh() const -> MeshNT * { return m_Mesh.get(); }

DEF ModelNT::enqueueIntoCommandBuffer(VkCommandBuffer commandBuffer, VkDescriptorSet descriptorSet) -> void {
    std::array vertexBuffers = {this->getMesh()->getVertexBuffer()};
    std::array<VkDeviceSize, 1> offsets = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers.data(), offsets.data());

    vkCmdBindIndexBuffer(commandBuffer, this->getMesh()->getVertexIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_Engine->getPipelineLayout(),
        0,
        1,
        &descriptorSet,
        0,
        nullptr);

    vkCmdDrawIndexed(
        commandBuffer,
        static_cast<uint32_t>(this->getMesh()->getVertexIndices().size()),
        1,
        0,
        0,
        0);
}

DEF ModelNT::getUBO() -> UniformBufferObject {
    static auto startTime = std::chrono::high_resolution_clock::now();
    auto currentTime = std::chrono::high_resolution_clock::now();
    float delta_time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

    mat4 view = lookAt(m_Engine->m_CameraEye, m_Engine->m_CameraCenter, m_Engine->m_CameraUp);
    // TODO: Read those values from constants
    mat4 proj = glm::perspective(
        PI_QUARTER,
        static_cast<float>(m_Engine->getSwapchainExtent().width) / static_cast<float>(m_Engine->getSwapchainExtent().height),
        Settings::CLIPPING_PLANE_NEAR, Settings::CLIPPING_PLANE_FAR);

    proj[1][1] *= -1;

    mat4 modelMatrix = this->getMatrix();

    UniformBufferObject ubo{
        .model = modelMatrix,
        .view = view,
        .proj = proj};
    return ubo;
}
