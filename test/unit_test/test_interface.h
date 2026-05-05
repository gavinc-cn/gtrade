//
// Created by dell on 2024/11/14.
//

#pragma once

struct sample {
    int id;
    std::string name;
    int age;

    inline sample() = default;
    inline sample(int id, std::string name, int age) : id(id), name(name), age(age) {}
};