#pragma once

#include <QMainWindow>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QGroupBox>
#include <QSpinBox>

#include <memory>
#include <vector>
#include <utility>

#include "pipeline.hpp"
#include "cache.hpp"
#include "dram.hpp"
#include "instruction.hpp"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void onLoadFile();
    void onStep();

private:
    void setupUI();

    void initSimulator(const std::vector<Instruction>&               program,
                       const std::vector<std::pair<int,DRAM::Line>>& dataBlocks);

    void refreshAll();
    void refreshPipelineDisplay();
    void refreshInstructions();
    void refreshRegisters();
    void refreshCache();
    void refreshDRAM();

    static void  setStageStyle(QLabel* label, const QString& color, const QString& text);
    static QString registerName(int reg);   // "r0 (zero)", "r1 (ra)", etc.
    static QString flagsDescription(int r31Value);  // decode bits 3:0

    // -----------------------------------------------------------------------
    // Simulator state
    // -----------------------------------------------------------------------
    std::unique_ptr<DRAM>     dram_;
    std::unique_ptr<Cache>    cache_;
    std::unique_ptr<Pipeline> pipeline_;
    std::vector<Instruction>  program_;
    bool simReady_ = false;

    // -----------------------------------------------------------------------
    // UI — pipeline stage boxes
    // -----------------------------------------------------------------------
    QLabel* stageIF_;
    QLabel* stageID_;
    QLabel* stageEX_;
    QLabel* stageMEM_;
    QLabel* stageWB_;
    QLabel* cycleLabel_;

    // -----------------------------------------------------------------------
    // UI — data panels
    // -----------------------------------------------------------------------
    QListWidget* instrList_;
    QListWidget* regList_;
    QListWidget* cacheList_;
    QListWidget* dramList_;

    // -----------------------------------------------------------------------
    // UI — memory configuration spinboxes
    // -----------------------------------------------------------------------
    QSpinBox* dramNumLinesSpin_;
    QSpinBox* dramLineSizeSpin_;
    QSpinBox* dramDelaySpin_;
    QSpinBox* cacheNumLinesSpin_;
    QSpinBox* cacheLineSizeSpin_;   // kept in sync with DRAM line size
    QSpinBox* cacheDelaySpin_;

    // -----------------------------------------------------------------------
    // UI — controls
    // -----------------------------------------------------------------------
    QPushButton* stepBtn_;
    QLabel*      statusLabel_;
};
