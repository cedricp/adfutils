#include "SDL.h"
#include "window_sdl.h"
#include <imgui_internal.h>
#include "timer.h"
#include "thread.h"
#include <codecvt>
#include <locale>
#include <iostream>
#include <sys/time.h>
#include <SerialIO.h>
#include <ADFWriter.h>

static auto serialport_getter = [](void *vec, int idx, const char **out_text)
{
    static char txt[100];
    auto &vector = *(static_cast<std::vector<SerialIO::SerialPortInformation> *>(vec));
    if (idx < 0 || idx >= static_cast<int>(vector.size()))
    {
        return false;
    }
    std::string name;
    quickw2a(vector[idx].portName, name);
    strcpy(txt, name.c_str());
    *out_text = txt;
    return true;
};

class Disk2AdfThread : public ASyncTask
{
    ArduinoFloppyReader::ADFWriter& m_adfwriter;
    bool m_abort = false;
public:
    enum Action{RETRY=2,CONTINUE,ABORT};
    int track = 0;
    ArduinoFloppyReader::DiskSurface side;
    int totalsectors = 0;
    int error_found = 0;
    Disk2AdfThread(ArduinoFloppyReader::ADFWriter& adfw) : m_adfwriter(adfw){

    }

    void abort(){
        m_abort = true;
    }

    void entry() override{
        bool isADF = true;
        bool hdMode = false;

        auto callback = [this](const int currentTrack, const ArduinoFloppyReader::DiskSurface currentSide,
                                        const int retryCounter, const int sectorsFound, const int badSectorsFound,
                                        const int totalSectors, const ArduinoFloppyReader::CallbackOperation operation) -> ArduinoFloppyReader::WriteResponse {

            track = currentTrack;
            side = currentSide;
            totalsectors = totalSectors;

            if (m_abort){
                return ArduinoFloppyReader::WriteResponse::wrAbort;
            }

            if (retryCounter > 20) {
                error_found = 1;
                while(error_found){
                    usleep(100000);
                    if (error_found == ABORT){
                        error_found = 0;
                        return ArduinoFloppyReader::WriteResponse::wrAbort;
                    }
                    if (error_found == RETRY){
                        error_found = 0;
                        return ArduinoFloppyReader::WriteResponse::wrRetry;
                    }
                    if (error_found == ABORT){
                        error_found = 0;
                        return ArduinoFloppyReader::WriteResponse::wrContinue;
                    }
                }
            }
            
            return ArduinoFloppyReader::WriteResponse::wrContinue;
        };

        ArduinoFloppyReader::ADFResult result;
        std::string filename = "C:\\Users\\cedri\\Desktop\\adf.adf";
        std::wstring wfn;
        quicka2w(filename, wfn);
        result = m_adfwriter.DiskToADF(wfn, hdMode, 80, callback);
        stop();
    }
};

class DiskExplorerThread : public ASyncLoopTask
{
public:
    Event m_ev_listdir;
    enum Operation{
        NOP,
        LIST_DIR,
        CHDIR,
        PARENTDIR,
    };

    DiskExplorerThread(ArduinoFloppyReader::ADFWriter& adf) : m_adfwriter(adf){

    }

    void entry() override {
        if (m_current_op == LIST_DIR){
            std::vector<std::string> list;
            if( !m_adfwriter.getDirectoryList("", list) ){
                stop();
            }
            m_ev_listdir.set_data((void*)&list);
            m_ev_listdir.execute();
        }

        m_current_op = NOP;
    }

    void get_dir(){
        if (m_current_op != NOP){
            return;
        }
        m_current_op = LIST_DIR;

    }

private:
    ArduinoFloppyReader::ADFWriter& m_adfwriter;
    Operation m_current_op = NOP;
    std::string m_current_op_arg;
};

class AdfUtilsWindow : public Event, Widget
{
    ArduinoFloppyReader::ADFWriter m_adfwriter;
    DiskExplorerThread* m_diskexplorer_thread = nullptr;
    Disk2AdfThread* m_disk2adf_thread = nullptr;
    std::vector<SerialIO::SerialPortInformation> m_serialports;
    int m_selectedserial = 0;
    bool m_serialportok = false;
    bool m_disk_available = false;
    unsigned long m_time;
    STATIC_CALLBACK_METHOD(on_list_dir, AdfUtilsWindow)
public:
    AdfUtilsWindow(Window_SDL *win) : Widget(win, "AdfTools")
    {
        set_maximized(true);
        set_movable(false);
        set_resizable(false);
        set_titlebar(false);
        SerialIO::enumSerialPorts(m_serialports);
        m_time = timestamp();
    }
    ~AdfUtilsWindow(){
        m_adfwriter.closeDevice();
    }

    CALLBACK_METHOD(on_list_dir){
        Event* sender_ev = sender_object;
        if (!sender_ev) return;

        std::vector<std::string>* pathlist = (std::vector<std::string>*)sender_object->get_data1();
        for(auto path : *pathlist){
            std::cout << path << ", ";
        }
        std::cout << std::endl;

    }

    void draw() override
    {
        check_disk();

        if (m_disk2adf_thread && !m_disk2adf_thread->is_running()){
            delete m_disk2adf_thread;
            m_disk2adf_thread = nullptr;
        }
        bool working = is_working();

        ImGui::BeginChild("GUI_CONTROL", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_None);
        if (working){
            ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
        }
        ImGui::BeginChild("COMPORT", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        if (ImGui::Combo("COM port", &m_selectedserial, serialport_getter, (void *)&m_serialports, m_serialports.size()))
        {
            openDevice();
        }
        if (working){
            ImGui::PopItemFlag();
            ImGui::PopStyleVar();
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("COMMANDS", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX, ImGuiWindowFlags_None);
        if (!working && m_serialportok && m_disk_available && ImGui::Button("Disk2ADF")){
            start_disk2adf();
        }
        ImGui::SameLine();
        if(!working && m_serialportok && m_disk_available && ImGui::Button("Explore")){
            explore_disk();
        }
        if ((m_disk2adf_thread || m_diskexplorer_thread) && ImGui::Button("Abort")){
            if (m_disk2adf_thread) m_disk2adf_thread->abort();
            if (m_diskexplorer_thread) stop_explorer_thread();
        }
        if (m_disk2adf_thread){
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Extarcting disk Cylinder %i / Head %i", m_disk2adf_thread->track, m_disk2adf_thread->side);
        }
        ImGui::SameLine();
        if(!working && ImGui::Button("Refresh")){
            openDevice();
        }
        ImGui::EndChild();

        ImGui::EndChild();

        check_thread_status();
    }

    bool is_working(){
        return m_disk2adf_thread != nullptr || m_diskexplorer_thread != nullptr;
    }

    void check_disk(){
        if(timestamp() - m_time < 1000){
            return;
        }
        
        m_time = timestamp();

        if (is_working()) return;

        if (m_serialportok && m_adfwriter.checkDisk()){
            m_disk_available = true;
        } else {
            m_disk_available = false;
            if (m_diskexplorer_thread) stop_explorer_thread();
        }
    }

    void explore_disk(){
        start_explorer_thread();
        m_diskexplorer_thread->get_dir();
    }

    void start_explorer_thread(){
        if (m_diskexplorer_thread) return;
        printf("Thead launched\n");
        m_diskexplorer_thread = new DiskExplorerThread(m_adfwriter);
        CONNECT_CALLBACK((&m_diskexplorer_thread->m_ev_listdir), on_list_dir);
        m_diskexplorer_thread->start(); 
    }

    void stop_explorer_thread(){
        if (!m_diskexplorer_thread) return;

        m_diskexplorer_thread->stop();
        m_diskexplorer_thread->join();
        delete m_diskexplorer_thread;
        m_diskexplorer_thread = nullptr;
    }

    unsigned long timestamp(void)
    {
        struct timeval tv;
        if (gettimeofday(&tv, NULL) < 0) return 0;
        return (unsigned long)((unsigned long)tv.tv_sec * 1000 + (unsigned long)tv.tv_usec/1000);
    }

    void openDevice(){
        stop_explorer_thread();

        m_adfwriter.closeDevice();
        if (m_adfwriter.openDevice(m_serialports[m_selectedserial].portName)){
            ArduinoFloppyReader::FirmwareVersion fwversion = m_adfwriter.getFirwareVersion();
            m_serialportok = true;
        } else {
            m_serialportok = false;
        }
    }

    void check_thread_status(){
        if (m_disk2adf_thread && m_disk2adf_thread->error_found){
            ImGui::OpenPopup("DISK IO");

            ImVec2 center = ImGui::GetMainViewport()->GetCenter();
            ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

            if (ImGui::BeginPopupModal("DISK IO", NULL, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("Disk error, retry ?");
                ImGui::Separator();

                if (ImGui::Button("OK", ImVec2(120, 0))) { m_disk2adf_thread->error_found = Disk2AdfThread::RETRY;ImGui::CloseCurrentPopup(); }
                ImGui::SetItemDefaultFocus();
                ImGui::SameLine();
                if (ImGui::Button("Abort", ImVec2(120, 0))) { m_disk2adf_thread->error_found = Disk2AdfThread::ABORT;ImGui::CloseCurrentPopup(); }
                if (ImGui::Button("Continue", ImVec2(120, 0))) { m_disk2adf_thread->error_found = Disk2AdfThread::CONTINUE;ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
            }
        }

        if (m_diskexplorer_thread && !m_diskexplorer_thread->is_running()){
            stop_explorer_thread();
        }
    }

    void start_disk2adf(){
        stop_explorer_thread();
        m_disk2adf_thread = new Disk2AdfThread(m_adfwriter);
        m_disk2adf_thread->start();
    }

};

class MainWindow : public Window_SDL
{
    AdfUtilsWindow *m_adftool;

public:
    MainWindow() : Window_SDL("AmigaDiskFile Tools", 1200, 900)
    {
        m_adftool = new AdfUtilsWindow(this);
    }

    virtual ~MainWindow()
    {
    }

    void draw(bool c) override
    {
        Window_SDL::draw(c);
    }
};

int main(int argc, char *argv[])
{
    App_SDL *app = App_SDL::get();
    Window_SDL *window = new MainWindow;
    ImGui::GetStyle().FrameRounding = 5.0;
    ImGui::GetStyle().ChildRounding = 5.0;
    ImGui::GetStyle().WindowRounding = 4.0;
    ImGui::GetStyle().GrabRounding = 4.0;
    ImGui::GetStyle().GrabMinSize = 4.0;
    window->set_minimum_window_size(800, 600);
    app->add_window(window);
    app->run();
    return 0;
}