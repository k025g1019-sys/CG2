#pragma once

#include <vector>

struct VertexData;

class Sphere {
public:
    void Generate(
        int subdivision
    );

    const std::vector<VertexData>& GetVertices() const {
        return vertices_;
    }

private:
    std::vector<VertexData> vertices_;
};