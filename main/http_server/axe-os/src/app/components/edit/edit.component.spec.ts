import { ComponentFixture, TestBed } from '@angular/core/testing';

import { EditComponent } from './edit.component';
import { provideHttpClient } from '@angular/common/http';
import { provideToastr } from 'ngx-toastr';
import { provideRouter } from '@angular/router';
import { FormControl, FormGroup } from '@angular/forms';
import { provideNoopAnimations } from '@angular/platform-browser/animations';

describe('EditComponent', () => {
  let component: EditComponent;
  let fixture: ComponentFixture<EditComponent>;

  beforeEach(() => {
    TestBed.configureTestingModule({
      imports: [EditComponent],
      providers: [provideHttpClient(), provideToastr(), provideRouter([]), provideNoopAnimations()]
    });
    fixture = TestBed.createComponent(EditComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });

  it('shows external display information instead of local panel controls', () => {
    component.form = new FormGroup({
      frequency: new FormControl(300),
      coreVoltage: new FormControl(1100),
      autofanspeed: new FormControl(true),
      minfanspeed: new FormControl(25),
      manualFanSpeed: new FormControl(70),
      temptarget: new FormControl(60),
      display: new FormControl('SSD1306 (128x32)'),
      rotation: new FormControl(0),
      displayTimeout: new FormControl(-1),
      invertscreen: new FormControl(false),
      statsFrequency: new FormControl(30),
    });
    component.externalDisplay = true;

    fixture.detectChanges();

    const text = fixture.nativeElement.textContent;
    expect(text).toContain('external bonanzaDisplay');
    expect(text).not.toContain('Invert Display Colors');
  });
});
