#pragma once

#include <QMainWindow>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QGroupBox>
#include <QSpinBox>
#include <QRadioButton>
#include <QButtonGroup>

#include <memory>
#include <vector>
#include <utility>

#include "simmode.hpp"
#include "pipeline.hpp"
#include "seqexecutor.hpp"
#include "cache.hpp"
#include "directmemif.hpp"
#include "dram.hpp"
#include "instruction.hpp"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void onLoadFile();
    void onStep();
    void onRunToCompletion();

private:
    void setupUI();

    void initSimulator(const std::vector<Instruction>&               program,
                       const std::vector<std::pair<int,DRAM::Line>>& dataBlocks);

    void refreshAll();
    void refreshPipelineDisplay();
    void refreshSeqDisplay();
    void refreshInstructions();
    void refreshRegisters();
    void refreshCache();
    void refreshDRAM();
    void refreshInfoBar();   // updates cycle + PC labels

    static void    setStageStyle    (QLabel* label, const QString& color, const QString& text);
    static QString registerName     (int reg);
    static QString flagsDescription (int r31Value);

    // -----------------------------------------------------------------------
    // Simulator objects
    // -----------------------------------------------------------------------
    SimMode currentMode_ = SimMode::BOTH;

    std::unique_ptr<DRAM>               dram_;
    std::unique_ptr<Cache>              cache_;
    std::unique_ptr<DirectMemIF>        directMem_;
    std::unique_ptr<Pipeline>           pipeline_;
    std::unique_ptr<SequentialExecutor> seqExec_;

    std::vector<Instruction> program_;
    bool simReady_ = false;

    // -----------------------------------------------------------------------
    // UI — mode selector
    // -----------------------------------------------------------------------
    QRadioButton* modeNoPipeNoCache_;
    QRadioButton* modePipeOnly_;
    QRadioButton* modeCacheOnly_;
    QRadioButton* modeBoth_;
    QButtonGroup* modeGroup_;
    QLabel*       modeDescLabel_;

    // -----------------------------------------------------------------------
    // UI — info bar (cycle count + PC)
    // -----------------------------------------------------------------------
    QLabel* cycleValueLabel_;  // shows the cycle number
    QLabel* pcValueLabel_;     // shows the current PC

    // -----------------------------------------------------------------------
    // UI — top display: pipeline stage boxes OR sequential instruction box
    // -----------------------------------------------------------------------
    QWidget* pipeWidget_;
    QWidget* seqWidget_;

    QLabel* stageIF_;
    QLabel* stageID_;
    QLabel* stageEX_;
    QLabel* stageMEM_;
    QLabel* stageWB_;
    QLabel* seqInstrLabel_;

    // -----------------------------------------------------------------------
    // UI — data panels
    // -----------------------------------------------------------------------
    QListWidget* instrList_;
    QListWidget* regList_;
    QListWidget* cacheList_;
    QListWidget* dramList_;
    QGroupBox*   cachePanel_;

    // -----------------------------------------------------------------------
    // UI — memory configuration
    // -----------------------------------------------------------------------
    QSpinBox*  dramNumLinesSpin_;
    QSpinBox*  dramLineSizeSpin_;
    QSpinBox*  dramDelaySpin_;
    QSpinBox*  cacheNumLinesSpin_;
    QSpinBox*  cacheLineSizeSpin_;
    QSpinBox*  cacheDelaySpin_;
    QGroupBox* cacheCfgBox_;
    QLabel*    dramCapLabel_;

    // -----------------------------------------------------------------------
    // UI — controls
    // -----------------------------------------------------------------------
    QPushButton* stepBtn_;
    QPushButton* runBtn_;
    QLabel*      statusLabel_;
};
