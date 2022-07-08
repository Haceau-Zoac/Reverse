#include <windows.h>
#include <windowsx.h>
#include <D2D1.h>
#include <dwrite.h>
#include <atlbase.h>
#include <vector>
#include <algorithm>
#include <functional>
#include <stdexcept>

HWND hwnd;
CComPtr<ID2D1Factory> factory{};
CComPtr<ID2D1HwndRenderTarget> renderTarget{};
CComPtr<ID2D1SolidColorBrush> buttonBorderBrush{}, buttonNormalBrush{}, buttonHoverBrush{}, textBoxBorderBrush{}, textWriteBrush{};

bool PointInRectangle(D2D1_RECT_F rectangle, D2D1_POINT_2U point) {
	return rectangle.top < point.y
		&& rectangle.bottom > point.y
		&& rectangle.left < point.x
		&& rectangle.right > point.x;
}

class TextWriter {
public:
	static TextWriter& GetInstance() {
		static TextWriter textWriter;
		return textWriter;
	}

	void Draw(D2D1_RECT_F area, std::wstring_view text) {
		renderTarget->DrawTextW(text.data(), static_cast<unsigned>(text.size()), _textFormat, &area, textWriteBrush);
	}

private:
	CComPtr<IDWriteFactory> _directWriteFactory;
	CComPtr<IDWriteTextFormat> _textFormat;

	TextWriter() {
		HRESULT hr = DWriteCreateFactory(
				DWRITE_FACTORY_TYPE_SHARED,
				__uuidof(IDWriteFactory),
				reinterpret_cast<IUnknown**>(&_directWriteFactory));
		if (FAILED(hr)) {
			throw std::runtime_error("Initialize TextWriter failed: DWriteCreateFactory.");
		}

		hr = _directWriteFactory->CreateTextFormat(
			L"Sarasa Fixed CL", nullptr,
			DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
			14.f, L"en-us", &_textFormat);
		if (FAILED(hr)) {
			throw std::runtime_error("Initialize TextWriter failed: CreateTextFormat.");
		}

		hr = _textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
		if (FAILED(hr)) {
			throw std::runtime_error("Initialize TextWriter failed: SetTextAlignment.");
		}

		hr = _textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
		if (FAILED(hr)) {
			throw std::runtime_error("Initialize TextWriter failed: SetParagraphAlignment");
		}
	}
};

#undef SendMessage
#undef GetMessage

class Control {
protected:
	D2D1_RECT_F _area;
	bool _onHover{ false };
	bool _onClick{ false };
	bool _onFocus{ false };
	std::function<void()> _clickEvent{ []() {} };
	std::function<void()> _changeEvent{ []() {} };
public:
	Control(D2D1_RECT_F area);
	virtual ~Control();

	virtual void Show();
	virtual void Paint();
	virtual void OnHover(D2D1_POINT_2U point);
	virtual void OnClick(D2D1_POINT_2U click);
	virtual void OnFocus();
	virtual void OnKeyDown(unsigned key);
	virtual void OnChar(wchar_t ch);
	virtual void LeaveClick();
	virtual void LeaveHover();
	virtual void LeaveFocus();
	bool IsHover() const;
	bool IsClicked() const;
	bool IsFocused() const;
	void WhenClick(std::function<void()>&& f);
	void WhenChange(std::function<void()>&& f);
	template<typename T>
	void SendMessage(T* to) {
		to->GetMessage(this);
	}
	template<typename T, typename U>
	void SendMessage(T* to, U* data) {
		to->GetMessage(this, data);
	}
	D2D1_RECT_F const& Area() const;
};

class ControlContainer {
private:
	ControlContainer() {}
	~ControlContainer() {
		for (auto control : _controls) {
			delete control;
		}
	}
	std::vector<Control*> _controls;
public:
	void Add(Control* control) {
		_controls.emplace_back(control);
	}

	void OnHover(unsigned x, unsigned y) {
		for (auto control : _controls) {
			if (PointInRectangle(control->Area(), { x, y })) {
				if (!control->IsHover()) {
					control->OnHover({ x, y });
				}
			} else if (control->IsHover()) {
				control->LeaveHover();
			}
		}
	}

	void OnClick(unsigned x, unsigned y) {
		for (auto control : _controls) {
			if (PointInRectangle(control->Area(), { x, y })) {
				control->OnClick({ x, y });
				control->OnFocus();
			} else if (control->IsFocused()) {
				control->LeaveFocus();
			}
		}
	}
	void OnChar(WPARAM ch) {
		for (auto control : _controls) {
			if (control->IsFocused()) {
				control->OnChar(static_cast<wchar_t>(ch));
				break;
			}
		}
	}
	void OnKeyDown(WPARAM key) {
		for (auto control : _controls) {
			if (control->IsFocused()) {
				control->OnKeyDown(static_cast<unsigned>(key));
				break;
			}
		}
	}
	
	void LeaveClick() {
		for (auto control : _controls) {
			if (control->IsClicked()) {
				control->LeaveClick();
			}
		}
	}

	void Paint() {
		for (auto control : _controls) {
			control->Paint();
		}
	}

	static ControlContainer& GetInstance() {
		static ControlContainer instance;
		return instance;
	}
};

Control::Control(D2D1_RECT_F area)
	: _area(area) {
	ControlContainer::GetInstance().Add(this);
}
Control::~Control() {}
void Control::Show() {}
void Control::Paint() {}
void Control::OnHover(D2D1_POINT_2U point) { _onHover = true; }
void Control::OnClick(D2D1_POINT_2U click) { _onClick = true; }
void Control::OnFocus() { _onFocus = true; }
void Control::OnKeyDown(unsigned key) {}
void Control::OnChar(wchar_t ch) {}
void Control::LeaveClick() { _onClick = false; _clickEvent(); }
void Control::LeaveHover() { _onHover = false; }
void Control::LeaveFocus() { _onFocus = false; }
bool Control::IsHover() const { return _onHover; }
bool Control::IsClicked() const { return _onClick; }
bool Control::IsFocused() const { return _onFocus; }
void Control::WhenClick(std::function<void()>&& f) { _clickEvent = std::forward<std::function<void()>>(f); }
void Control::WhenChange(std::function<void()>&& f) { _changeEvent = std::forward<std::function<void()>>(f); }
D2D1_RECT_F const& Control::Area() const { return _area; }

class Label : public Control {
private:
	std::wstring _text{};
public:
	using Control::Control;

	Label(D2D1_RECT_F area, std::wstring text)
		: Control(area), _text(text)
	{}

	void Paint() override {
		TextWriter::GetInstance().Draw(_area, _text);
	}

	void Text(std::wstring text) {
		_text = text;
	}
};

class TextBox : public Control {
private:
	std::wstring _text;
public:
	using Control::Control;

	void Paint() override{
		renderTarget->DrawRectangle(_area, textBoxBorderBrush);
		TextWriter::GetInstance().Draw(_area, _text);
	}
	void OnChar(wchar_t ch) override {
		if (ch != '\b') {
			_text += ch;
			_changeEvent();
		}
	}
	void OnKeyDown(unsigned key) override {
		if (key == VK_BACK && !_text.empty()) {
			_text.pop_back();
			_changeEvent();
		}
	}
	std::wstring Text() const {
		return _text;
	}
};

class Button : public Control {
private:
	ID2D1SolidColorBrush* GetBrush() {
		return _onHover ? buttonHoverBrush : buttonNormalBrush;
	}
public:
	using Control::Control;

	void Paint() override {
		renderTarget->FillRectangle(_area, GetBrush());
	}
};

void UserInterface() {
	TextBox* input = new TextBox{ D2D1::RectF(20.f, 20.f, 150.f, 50.f) };
	Label* output = new Label{ D2D1::RectF(20.f, 60.f, 150.f, 85.f) };
	input->WhenChange([=]() {
		auto text{ input->Text() };
		std::reverse(text.begin(), text.end());
		output->Text(text);
	});
}

void CreateD2DResource(HWND hWnd)
{
	if (!renderTarget)
	{
		HRESULT hr;

		hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory);
		if (FAILED(hr))
		{
			MessageBoxW(hWnd, L"Create D2D factory failed!", L"Error", 0);
			return;
		}

		// Obtain the size of the drawing area
		RECT rc;
		GetClientRect(hWnd, &rc);

		// Create a Direct2D render target
		hr = factory->CreateHwndRenderTarget(
			D2D1::RenderTargetProperties(),
			D2D1::HwndRenderTargetProperties(
				hWnd,
				D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top)
			),
			&renderTarget
		);
		if (FAILED(hr))
		{
			MessageBoxW(hWnd, L"Create render target failed!", L"Error", 0);
			return;
		}

		// Create a brush
		hr = renderTarget->CreateSolidColorBrush(D2D1::ColorF(0xF7F7F7, 1.f), &buttonNormalBrush)
			| renderTarget->CreateSolidColorBrush(D2D1::ColorF(0xEAEAEA), &buttonHoverBrush)
			| renderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &textWriteBrush)
			| renderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Gray), &textBoxBorderBrush);
		if (FAILED(hr))
		{
			MessageBoxW(hWnd, L"Create brush failed!", L"Error", 0);
			return;
		}
	}
}

VOID DrawRectangle(HWND hwnd)
{
	CreateD2DResource(hwnd);

	renderTarget->BeginDraw();
	renderTarget->Clear(D2D1::ColorF(D2D1::ColorF::White));
	ControlContainer::GetInstance().Paint();

	HRESULT hr = renderTarget->EndDraw();
	if (FAILED(hr))
	{
		MessageBoxW(nullptr, L"Draw failed!", L"Error", MB_OK);
		return;
	}
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_PAINT:
		DrawRectangle(hwnd);
		return 0;
	case WM_MOUSEMOVE:
		ControlContainer::GetInstance().OnHover(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;
	case WM_LBUTTONDOWN:
		ControlContainer::GetInstance().OnClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;
	case WM_LBUTTONUP:
		ControlContainer::GetInstance().LeaveClick();
		return 0;
	case WM_CHAR:
		ControlContainer::GetInstance().OnChar(wParam);
		return 0;
	case WM_KEYDOWN:
		ControlContainer::GetInstance().OnKeyDown(wParam);
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	case WM_SIZE: {
		D2D1_SIZE_U resize{ .width = LOWORD(lParam), .height = HIWORD(lParam) };
		if (renderTarget != nullptr) {
			renderTarget->Resize(&resize);
		}
		return 0;
	}
	}

	return DefWindowProcW(hwnd, message, wParam, lParam);
}

int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR szCmdLine, int iCmdShow)
{
	WNDCLASSEX winClass{};

	winClass.lpszClassName = L"Direct2D";
	winClass.cbSize = sizeof(WNDCLASSEX);
	winClass.style = CS_HREDRAW | CS_VREDRAW;
	winClass.lpfnWndProc = WndProc;
	winClass.hInstance = hInstance;
	winClass.hIcon = NULL;
	winClass.hIconSm = NULL;
	winClass.hCursor = LoadCursor(NULL, IDC_ARROW);
	winClass.hbrBackground = NULL;
	winClass.lpszMenuName = NULL;
	winClass.cbClsExtra = 0;
	winClass.cbWndExtra = 0;

	if (!RegisterClassEx(&winClass))
	{
		MessageBoxW(nullptr, L"This program requires Windows NT!", L"error", MB_ICONERROR);
		return 0;
	}

	hwnd = CreateWindowExW(NULL,
		L"Direct2D",					// window class name
		L"Draw Rectangle",			// window caption
		WS_OVERLAPPEDWINDOW, 		// window style
		CW_USEDEFAULT,				// initial x position
		CW_USEDEFAULT,				// initial y position
		600,						// initial x size
		600,						// initial y size
		NULL,						// parent window handle
		NULL,						// window menu handle
		hInstance,					// program instance handle
		NULL);						// creation parameters

	UserInterface();

	ShowWindow(hwnd, iCmdShow);
	UpdateWindow(hwnd);

	MSG msg{};
	while (GetMessageW(&msg, nullptr, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	return static_cast<int>(msg.wParam);
}
